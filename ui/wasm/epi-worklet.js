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
      default: break;
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
      });
    }
    return true;
  }
}

registerProcessor ('epi-processor', EpiProcessor);
