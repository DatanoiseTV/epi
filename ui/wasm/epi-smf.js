/*
   Epi — Standard MIDI Files, parsed.

   Turns a .mid into a flat, time-sorted list of events in SECONDS, plus enough
   description of what is in it to decide which parts are the piano.

   The unit of selection is a PART -- one channel within one track -- and not a
   track, because the two file layouts disagree about which of those carries
   the structure:

     Format 0 is a single track with every channel in it. There is nothing to
     choose between at track level; the parts are the channels.

     Format 1 is several simultaneous tracks sharing one timeline, usually a
     channel each, sometimes not. The parts are still channels; they just
     happen to line up with tracks.

   Scoring parts rather than tracks means one reader handles both, and handles
   the awkward middle case -- a format 1 file with a piano and a bass on the
   same track, on different channels -- which neither of the obvious
   simplifications does.

   Three more things a naive reader gets wrong:

     The tempo is not a number, it is a map. A set-tempo meta event can appear
     anywhere in any track and applies from that tick onward, so ticks become
     seconds by integrating over the changes, not by dividing by one number
     taken from the top of the file. A piece with a ritardando played through
     a single tempo drifts further out with every bar.

     Each track's delta times restart at zero, so ticks accumulate per track
     and the tracks are merged afterwards.

     Running status: a channel message may omit its status byte and inherit
     the previous one. Meta and system-exclusive events do NOT set it, and a
     reader that lets them clobber it desynchronises at the next
     running-status event and produces garbage from there on.
*/
(function (global) {
  'use strict';

  // General MIDI 0-7 are the piano family: grand, bright, electric grand,
  // honky-tonk, two electric pianos, harpsichord, clavi. Epi is four of those
  // and a grand, so the whole family is what it should be playing.
  const PIANO_PROGRAM_MAX = 7;
  const DRUM_CHANNEL = 9;                    // channel 10, one-based

  const PIANO_WORDS = [
    'piano', 'pno', 'keys', 'keyboard', 'rhodes', 'wurli', 'wurlitzer',
    'clav', 'clavinet', 'epiano', 'e.piano', 'e-piano', 'elec piano',
    'grand', 'upright', 'harpsi', 'fender', 'suitcase', 'stage'
  ];
  // Checked first: a stem called "piano bass" is a bass part, and a track
  // called "no piano" is a stripped mix.
  const NOT_PIANO_WORDS = ['bass', 'drum', 'perc', 'kick', 'snare', 'hat', 'click', 'metronome'];

  function readVarInt (v, s) {
    let value = 0, byte = 0, n = 0;
    do {
      if (s.p >= v.length) { s.p = v.length; break; }
      byte = v[s.p++];
      value = (value << 7) | (byte & 0x7f);
      if (++n > 4) break;
    } while (byte & 0x80);
    return value >>> 0;
  }

  function readString (v, p, len) {
    let s = '';
    for (let i = 0; i < len && p + i < v.length; i++) s += String.fromCharCode (v[p + i]);
    return s;
  }

  function parse (arrayBuffer) {
    const v = new Uint8Array (arrayBuffer);
    if (v.length < 14 || readString (v, 0, 4) !== 'MThd')
      throw new Error ('not a MIDI file (no MThd header)');

    const headerLen = (v[4] << 24) | (v[5] << 16) | (v[6] << 8) | v[7];
    const format = (v[8] << 8) | v[9];
    const numTracks = (v[10] << 8) | v[11];
    const division = (v[12] << 8) | v[13];

    // SMPTE division: the top bit set means frames per second in the high byte
    // (as a negative) and ticks per frame in the low one. A file using it has
    // absolute time and its tempo map, if any, does not apply.
    let ticksPerQuarter = division & 0x7fff;
    let smpteTicksPerSecond = 0;
    if (division & 0x8000) {
      const fps = 256 - ((division >> 8) & 0xff);
      smpteTicksPerSecond = fps * (division & 0xff);
      ticksPerQuarter = 0;
    }
    if (!smpteTicksPerSecond && !ticksPerQuarter) ticksPerQuarter = 480;

    let p = 8 + headerLen;
    const trackNames = [];
    const events = [];                       // every channel event, in ticks
    const tempoChanges = [];

    for (let t = 0; t < numTracks && p + 8 <= v.length; t++) {
      // Skip any chunk that is not a track, as the specification requires.
      if (readString (v, p, 4) !== 'MTrk') {
        const skip = (v[p + 4] << 24) | (v[p + 5] << 16) | (v[p + 6] << 8) | v[p + 7];
        p += 8 + (skip >= 0 ? skip : 0);
        t--;
        continue;
      }
      const len = (v[p + 4] << 24) | (v[p + 5] << 16) | (v[p + 6] << 8) | v[p + 7];
      const start = p + 8;
      const end = Math.min (v.length, start + len);
      p = end;

      const trackIndex = trackNames.length;
      trackNames.push ({ name: '', instrument: '' });

      const s = { p: start };
      let tick = 0;
      let running = 0;                       // the last CHANNEL status, only

      while (s.p < end) {
        tick += readVarInt (v, s);
        if (s.p >= end) break;

        // Running status is inherited from the last CHANNEL message. Meta
        // (0xFF) and system messages (0xF0-0xF7) have the high bit set too,
        // so reading "high bit means new status" and storing it unconditionally
        // lets a track name overwrite the running status -- and every
        // running-status event after it is then read as meta. The result is
        // not a crash, it is a track that silently stops partway through.
        const b = v[s.p];
        let status;
        if (b & 0x80) {
          status = b;
          s.p++;
          if (b < 0xf0) running = b;
        } else {
          status = running;
          if (!status) { s.p++; continue; }   // data before any status: skip it
        }

        if (status === 0xff) {
          const type = v[s.p++];
          const len2 = readVarInt (v, s);
          const text = readString (v, s.p, len2);
          if (type === 0x03 && !trackNames[trackIndex].name) trackNames[trackIndex].name = text.trim();
          else if (type === 0x04 && !trackNames[trackIndex].instrument) trackNames[trackIndex].instrument = text.trim();
          else if (type === 0x51 && len2 === 3)
            tempoChanges.push ({ tick, usPerQuarter: (v[s.p] << 16) | (v[s.p + 1] << 8) | v[s.p + 2] });
          s.p += len2;
          if (type === 0x2f) break;
          continue;
        }

        if (status === 0xf0 || status === 0xf7) {
          const len2 = readVarInt (v, s);
          s.p += len2;
          continue;
        }

        const kind = status & 0xf0;
        const channel = status & 0x0f;
        if (s.p >= end) break;
        const d1 = v[s.p++];
        const twoBytes = kind !== 0xc0 && kind !== 0xd0;
        const d2 = twoBytes ? (s.p < end ? v[s.p++] : 0) : 0;

        if (kind === 0x90 && d2 > 0)
          events.push ({ tick, track: trackIndex, channel, kind: 'on', note: d1, value: d2 / 127 });
        else if (kind === 0x80 || (kind === 0x90 && d2 === 0))
          events.push ({ tick, track: trackIndex, channel, kind: 'off', note: d1, value: 0 });
        else if (kind === 0xb0)
          events.push ({ tick, track: trackIndex, channel, kind: 'cc', note: d1, value: d2 / 127 });
        else if (kind === 0xc0)
          events.push ({ tick, track: trackIndex, channel, kind: 'program', note: d1, value: 0 });
      }
    }

    // ---- ticks to seconds ------------------------------------------------
    tempoChanges.sort ((a, b) => a.tick - b.tick);
    const map = [{ tick: 0, seconds: 0, usPerQuarter: 500000 }];       // 120 bpm until told
    if (tempoChanges.length && tempoChanges[0].tick === 0)
      map[0].usPerQuarter = tempoChanges[0].usPerQuarter;

    const tickSeconds = (ticks, usPerQuarter) =>
      smpteTicksPerSecond ? ticks / smpteTicksPerSecond
                          : ticks * (usPerQuarter / 1e6) / ticksPerQuarter;

    for (const c of tempoChanges) {
      if (c.tick === 0) continue;
      const last = map[map.length - 1];
      map.push ({
        tick: c.tick,
        seconds: last.seconds + tickSeconds (c.tick - last.tick, last.usPerQuarter),
        usPerQuarter: c.usPerQuarter,
      });
    }

    // Events are already in tick order per track but not across tracks, and
    // the tempo map is walked forwards, so sort before converting.
    events.sort ((a, b) => a.tick - b.tick);
    let mi = 0;
    for (const e of events) {
      while (mi + 1 < map.length && map[mi + 1].tick <= e.tick) mi++;
      e.time = map[mi].seconds + tickSeconds (e.tick - map[mi].tick, map[mi].usPerQuarter);
    }

    // ---- parts: one channel within one track -----------------------------
    const parts = new Map();
    const keyOf = (e) => e.track + ':' + e.channel;

    for (const e of events) {
      const key = keyOf (e);
      let part = parts.get (key);
      if (!part) {
        part = {
          key, track: e.track, channel: e.channel,
          name: trackNames[e.track] ? trackNames[e.track].name : '',
          instrument: trackNames[e.track] ? trackNames[e.track].instrument : '',
          programs: new Set(), noteCount: 0, lowest: 128, highest: -1,
          first: e.time, last: e.time,
        };
        parts.set (key, part);
      }
      part.last = Math.max (part.last, e.time);
      if (e.kind === 'program') part.programs.add (e.note);
      else if (e.kind === 'on') {
        part.noteCount++;
        part.lowest = Math.min (part.lowest, e.note);
        part.highest = Math.max (part.highest, e.note);
      }
    }

    const duration = events.length ? events[events.length - 1].time : 0;

    return {
      format, numTracks: trackNames.length, division, ticksPerQuarter,
      smpte: smpteTicksPerSecond > 0,
      trackNames, events, parts: [...parts.values()],
      duration,
      tempoChanges: map.length,
      startBpm: 60e6 / map[0].usPerQuarter,
    };
  }

  // ---- which parts are the piano ----------------------------------------
  //
  // Scored rather than picked, because the evidence disagrees more often than
  // not: a file may name its tracks and set no programs, or set programs and
  // name nothing, or -- format 0 -- have no track names to speak of at all.
  function scoreParts (parsed) {
    return parsed.parts.map ((t) => {
      const name = (t.name + ' ' + t.instrument).toLowerCase();
      let score = 0;
      const why = [];

      // Channel 10 is percussion in General MIDI and is never a piano part,
      // whatever it is called.
      if (t.channel === DRUM_CHANNEL) return { key: t.key, score: -100, why: ['drum channel'], part: t };

      const programs = [...t.programs];
      if (programs.some ((x) => x <= PIANO_PROGRAM_MAX)) { score += 6; why.push ('piano program'); }
      else if (programs.length > 0) { score -= 3; why.push ('another instrument'); }

      // A name only counts for something when it can belong to this part. In
      // format 0 every part shares the one track name, so it says nothing
      // about which channel is the piano and is ignored.
      const nameIsThisPart = parsed.format !== 0;
      if (nameIsThisPart && name.trim()) {
        if (NOT_PIANO_WORDS.some ((w) => name.includes (w))) { score -= 5; why.push ('named as another part'); }
        else if (PIANO_WORDS.some ((w) => name.includes (w))) { score += 5; why.push ('named as a piano'); }
      }

      // A piano part covers ground. Something that never leaves an octave is
      // more likely a bass line or a pad.
      const span = t.highest - t.lowest;
      if (t.noteCount > 16 && span > 24) { score += 2; why.push ('wide range'); }
      if (t.noteCount === 0) { score -= 10; why.push ('no notes'); }

      return { key: t.key, score, why, part: t };
    });
  }

  // The parts to play, and whether the choice was confident. A file with no
  // evidence either way gets everything that is not drums: playing the whole
  // piece is a better failure than playing none of it.
  function choosePiano (parsed) {
    const scored = scoreParts (parsed);
    const withNotes = scored.filter ((s) => s.part.noteCount > 0);
    if (withNotes.length === 0) return { selected: [], confident: false, scored };

    const best = Math.max (...withNotes.map ((s) => s.score));
    if (best >= 5)
      return {
        selected: withNotes.filter ((s) => s.score >= Math.max (5, best - 1)).map ((s) => s.key),
        confident: true, scored,
      };

    return {
      selected: withNotes.filter ((s) => s.score >= 0).map ((s) => s.key),
      confident: false, scored,
    };
  }

  // ---- one flat schedule ------------------------------------------------
  // Sorted by time, with note-offs before note-ons at the same instant so a
  // repeated note releases its key before the next strike rather than after.
  function schedule (parsed, partKeys) {
    const wanted = new Set (partKeys);
    const out = [];
    for (const e of parsed.events) {
      if (!wanted.has (e.track + ':' + e.channel)) continue;
      if (e.kind === 'program') continue;
      // Only the three pedals: the rest of a file's controller traffic is
      // aimed at a synth that is not this one, and letting it move Epi's own
      // parameters would rewrite the instrument mid-piece.
      if (e.kind === 'cc' && e.note !== 64 && e.note !== 66 && e.note !== 67) continue;
      out.push (e);
    }
    const rank = (e) => (e.kind === 'off' ? 0 : e.kind === 'cc' ? 1 : 2);
    out.sort ((a, b) => (a.time - b.time) || (rank (a) - rank (b)));
    return out;
  }

  global.__EPI_SMF__ = { parse, choosePiano, schedule, scoreParts };
}) (typeof window !== 'undefined' ? window : globalThis);
