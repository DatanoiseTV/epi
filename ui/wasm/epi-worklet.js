/*
   Epi — the audio thread, in a browser.

   An AudioWorkletProcessor that owns the whole instrument. The WebAssembly
   module arrives already compiled from the main thread, because a worklet
   cannot fetch, and it is instantiated with NO import object at all: the
   build is standalone wasm with zero imports, so there is no Emscripten
   runtime in here and nothing to go wrong in a scope that is neither a window
   nor a worker.

   The render quantum is 128 frames and the engine's block is 128, so a
   process() call is exactly one engine block and nothing is buffered between
   them.

   Memory does not grow -- the build fixes it at 32 MB -- so the heap views
   are taken once in the constructor rather than rebuilt every quantum, which
   at 375 quanta a second is worth doing.
*/

class EpiProcessor extends AudioWorkletProcessor {
  constructor (options) {
    super();

    const { wasmModule, rawParams, telemetryHz } = options.processorOptions;
    const E = new WebAssembly.Instance (wasmModule, {}).exports;
    this.E = E;

    E.epi_create (sampleRate, 128);
    if (rawParams) rawParams.forEach ((v, i) => E.epi_set_param (i, v));

    this.heap = new Float32Array (E.memory.buffer);
    this.heapU32 = new Uint32Array (E.memory.buffer);

    this.lp = E.epi_out_l() >> 2;
    this.rp = E.epi_out_r() >> 2;
    this.tp = E.epi_telemetry() >> 2;
    this.kp = E.epi_keys() >> 2;
    this.teleLen = E.epi_telemetry_size();
    this.keyWords = E.epi_key_words();

    // Telemetry is for drawing, so it runs at a frame rate, not an audio
    // rate: one packed message every N quanta rather than 375 a second.
    this.every = Math.max (1, Math.round ((sampleRate / 128) / (telemetryHz || 30)));
    this.tick = 0;

    // ---- the file player ------------------------------------------------
    // The score lives HERE, not on the main thread, and is played from the
    // worklet's own sample clock. A player that fires events from a timer
    // quantises every note to a block boundary -- 2.7 ms at 128 frames and
    // 48 kHz -- and that is before setTimeout's own jitter and any garbage
    // collection pause. Held here, each event lands on the sample the score
    // asks for.
    this.score = null;                 // { times, types, notes, values }
    this.scoreCount = 0;
    this.cursor = 0;
    this.playing = false;
    this.scoreTime = 0;                // seconds into the piece
    this.types = {
      0: E.epi_type_note_off(), 1: E.epi_type_note_on(),
      2: E.epi_type_sustain(), 3: E.epi_type_sostenuto(), 4: E.epi_type_soft(),
    };

    this.port.onmessage = (e) => this.command (e.data);
    this.port.postMessage ({ t: 'ready', sampleRate,
                             traceLen: E.epi_trace_len(),
                             fieldPoints: E.epi_field_points(),
                             numTines: E.epi_num_tines(),
                             loNote: E.epi_lo_note() });
  }

  command (m) {
    const E = this.E;
    switch (m.t) {
      case 'param':      E.epi_set_param (m.i, m.v); break;
      case 'note':       E.epi_note (m.note, m.velocity, m.on ? 1 : 0); break;
      case 'sustain':    E.epi_sustain (m.v); break;
      case 'sostenuto':  E.epi_sostenuto (m.v ? 1 : 0); break;
      case 'soft':       E.epi_soft (m.v ? 1 : 0); break;
      case 'allOff':     E.epi_all_notes_off(); break;
      case 'expression': E.epi_expression (m.v); break;
      case 'bend':       E.epi_pitch_bend (m.v); break;
      case 'velMap':     E.epi_set_vel_map (m.v[0], m.v[1], m.v[2], m.v[3], m.v[4]); break;
      case 'tineMod':    E.epi_set_tine_mod (m.i, m.a, m.b); break;
      case 'stringMod':  E.epi_set_string_mod (m.i, m.a, m.b); break;
      case 'grandMod':   E.epi_set_grand_mod (m.i, m.a, m.b); break;
      case 'pickupMod':  E.epi_set_pickup_mod (m.i, m.a, m.b, m.c); break;
      case 'cabMod':     E.epi_set_cab_mod (m.v[0], m.v[1], m.v[2], m.v[3], m.v[4]); break;
      case 'micMod':     E.epi_set_mic_mod (m.v[0], m.v[1], m.v[2], m.v[3], m.v[4]); break;
      case 'micStage': {
        // Written straight into the engine's own array, then committed, so a
        // thirty-one element edit is one copy and one call.
        const p = E.epi_mic_stage() >> 2;
        for (let i = 0; i < 31; i++) this.heap[p + i] = m.v[i];
        E.epi_commit_mic_stage();
        break;
      }
      case 'read': this.port.postMessage ({ t: 'read', id: m.id, v: this.readArray (m.what) }); break;

      case 'score':
        this.score = m.score;
        this.scoreCount = m.score ? m.score.times.length : 0;
        this.scoreTime = 0;
        this.cursor = 0;
        this.playing = false;
        break;

      case 'transport':
        if (m.seek !== undefined) {
          this.scoreTime = m.seek;
          this.seekCursor();
          // Anything still sounding was struck before the seek and has no
          // business ringing after it.
          E.epi_all_notes_off();
          E.epi_sustain (0);
        }
        if (m.playing !== undefined) {
          this.playing = !!m.playing && this.scoreCount > 0;
          if (!this.playing) { E.epi_all_notes_off(); E.epi_sustain (0); }
        }
        break;
      default: break;
    }
  }

  seekCursor () {
    const t = this.score ? this.score.times : null;
    if (!t) { this.cursor = 0; return; }
    let lo = 0, hi = this.scoreCount;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (t[mid] < this.scoreTime) lo = mid + 1; else hi = mid;
    }
    this.cursor = lo;
  }

  // Fire everything the score puts inside this quantum, each at its own
  // sample. Returns after advancing the playhead by exactly one quantum, so
  // the piece runs on the audio clock and cannot drift against it.
  playQuantum (n) {
    if (!this.playing || !this.score) return;

    const { times, types, notes, values } = this.score;
    const dt = n / sampleRate;
    const t0 = this.scoreTime;
    const t1 = t0 + dt;

    while (this.cursor < this.scoreCount && times[this.cursor] < t1) {
      const i = this.cursor++;
      // An event whose time has already passed -- possible after a seek --
      // plays at the top of the block rather than being skipped.
      const off = Math.max (0, Math.min (n - 1, Math.round ((times[i] - t0) * sampleRate)));
      this.E.epi_event (this.types[types[i]] ?? 0, notes[i], values[i], off);
    }

    this.scoreTime = t1;
    if (this.cursor >= this.scoreCount) {
      this.playing = false;
      this.port.postMessage ({ t: 'ended' });
    }
  }

  readArray (what) {
    const E = this.E, n = E.epi_num_tines();
    const grab = (ptr, len) => Array.from (this.heap.subarray (ptr >> 2, (ptr >> 2) + len));
    switch (what) {
      case 'tineMods':   return grab (E.epi_tine_mods(),   n * 2);
      case 'stringMods': return grab (E.epi_string_mods(), n * 2);
      case 'grandMods':  return grab (E.epi_grand_mods(),  n * 2);
      case 'pickupMods': return grab (E.epi_pickup_mods(), n * 3);
      case 'cabMods':    return grab (E.epi_cab_mods(),    5);
      case 'micMods':    return grab (E.epi_mic_mods(),    5);
      case 'micStage':   return grab (E.epi_mic_stage(),   31);
      case 'velMap':     return grab (E.epi_vel_map(),     5);
      default:           return [];
    }
  }

  process (inputs, outputs) {
    const out = outputs[0];
    if (!out || out.length === 0) return true;

    const n = out[0].length;
    this.playQuantum (n);
    this.E.epi_process (n);

    out[0].set (this.heap.subarray (this.lp, this.lp + n));
    if (out.length > 1) out[1].set (this.heap.subarray (this.rp, this.rp + n));

    if (++this.tick >= this.every) {
      this.tick = 0;
      this.E.epi_pack_telemetry();
      this.port.postMessage ({
        t: 'telemetry',
        f: this.heap.slice (this.tp, this.tp + this.teleLen),
        k: Array.from (this.heapU32.subarray (this.kp, this.kp + this.keyWords)),
        // The playhead comes from the audio clock, so the transport shows
        // where the instrument actually is rather than where a timer thinks
        // it should be.
        pos: this.scoreTime,
        playing: this.playing,
      });
    }
    return true;
  }
}

registerProcessor ('epi-processor', EpiProcessor);
