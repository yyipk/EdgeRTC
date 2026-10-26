(() => {
  if (new URLSearchParams(location.search).get('video_debug') !== '1' ||
      !window.RTCPeerConnection || !window.RTCDataChannel) return;
  const peers = new WeakMap(), channels = new WeakMap();
  const pcPrototype = RTCPeerConnection.prototype, dcPrototype = RTCDataChannel.prototype;
  const create = pcPrototype.createDataChannel, getStats = pcPrototype.getStats;
  const close = pcPrototype.close, send = dcPrototype.send;
  let nextId = 0;
  const fields = (source, names) => Object.fromEntries(names.map(name => [name, source[name]]));
  function watch(pc) {
    if (peers.has(pc)) return peers.get(pc);
    const state = {id: ++nextId, channels: new Map(), stats: {}};
    peers.set(pc, state);
    const incoming = event => attach(state, event.channel);
    const cleanup = () => {
      if (pc.connectionState !== 'closed' && pc.signalingState !== 'closed') return;
      clearInterval(state.timer);
      pc.removeEventListener('datachannel', incoming);
      pc.removeEventListener('connectionstatechange', cleanup);
      pc.removeEventListener('signalingstatechange', cleanup);
      for (const [channel, entry] of state.channels) {
        channel.removeEventListener('message', entry.receive);
        channel.removeEventListener('close', entry.close);
        channels.delete(channel);
      }
      state.channels.clear();
      peers.delete(pc);
    };
    state.cleanup = cleanup;
    pc.addEventListener('datachannel', incoming);
    pc.addEventListener('connectionstatechange', cleanup);
    pc.addEventListener('signalingstatechange', cleanup);
    state.timer = setInterval(() => {
      const report = {id: state.id, time: Date.now(), stats: state.stats,
        channels: [...state.channels].map(([channel, entry]) => ({
          id: channel.id, state: channel.readyState, bufferedAmount: channel.bufferedAmount,
          maxRetransmits: channel.maxRetransmits, maxPacketLifeTime: channel.maxPacketLifeTime,
          ...entry.counts
        }))};
      if (typeof sessions !== 'undefined') for (const session of sessions.values()) {
        if (session.pc === pc) report.render = {state: session.state,
          rendered: session.mjpegRenderedFrames, dropped: session.mjpegDroppedFrames};
      }
      fetch('/?video_probe=' + encodeURIComponent(JSON.stringify(report)),
        {method: 'HEAD', cache: 'no-store'}).catch(() => {});
    }, 5000);
    return state;
  }
  function attach(state, channel) {
    if (channels.has(channel)) return;
    const entry = {counts: {frames: 0, chunks: 0, bytes: 0, throws: 0,
      messagesReceived: 0, bytesReceived: 0}};
    entry.receive = event => {
      entry.counts.messagesReceived++;
      const data = event.data;
      entry.counts.bytesReceived += typeof data === 'string' ?
        new TextEncoder().encode(data).length : data.byteLength ?? data.size ?? 0;
    };
    entry.close = () => {
      channel.removeEventListener('message', entry.receive);
      channel.removeEventListener('close', entry.close);
      channels.delete(channel);
      state.channels.delete(channel);
    };
    state.channels.set(channel, entry);
    channels.set(channel, entry);
    channel.addEventListener('message', entry.receive);
    channel.addEventListener('close', entry.close);
  }
  pcPrototype.createDataChannel = function (...args) {
    const channel = Reflect.apply(create, this, args);
    attach(watch(this), channel);
    return channel;
  };
  pcPrototype.close = function (...args) {
    const result = Reflect.apply(close, this, args);
    peers.get(this)?.cleanup();
    return result;
  };
  pcPrototype.getStats = function (...args) {
    const result = Reflect.apply(getStats, this, args);
    result.then(stats => {
      const state = peers.get(this);
      if (!state) return;
      const snapshot = {time: Date.now(), dataChannels: [], audio: []};
      let selected, fallback;
      stats.forEach(item => {
        if (item.type === 'transport' && item.selectedCandidatePairId)
          selected = stats.get(item.selectedCandidatePairId);
        if (item.type === 'candidate-pair' && item.state === 'succeeded' &&
            (item.selected || item.nominated) && (!fallback || item.selected)) fallback = item;
        if (item.type === 'data-channel') snapshot.dataChannels.push(fields(item,
          ['dataChannelIdentifier', 'messagesSent', 'bytesSent', 'messagesReceived', 'bytesReceived']));
        if (item.type === 'inbound-rtp' && (item.kind || item.mediaType) === 'audio')
          snapshot.audio.push(fields(item, ['packetsReceived', 'packetsLost', 'jitter', 'bytesReceived']));
      });
      if (selected || fallback) snapshot.pair = fields(selected || fallback,
        ['bytesSent', 'bytesReceived', 'packetsSent', 'packetsReceived', 'currentRoundTripTime']);
      state.stats = snapshot;
    }).catch(() => {});
    return result;
  };
  dcPrototype.send = function (...args) {
    const entry = channels.get(this), data = args[0];
    const view = entry && (ArrayBuffer.isView(data) ?
      new DataView(data.buffer, data.byteOffset, data.byteLength) :
      data instanceof ArrayBuffer ? new DataView(data) : null);
    const video = view && view.byteLength >= 4 && view.getUint16(0) === 0xffd8 &&
      view.getUint16(view.byteLength - 2) === 0xffd9;
    let result;
    try { result = Reflect.apply(send, this, args); }
    catch (error) { if (video) entry.counts.throws++; throw error; }
    if (video) {
      entry.counts.chunks++;
      entry.counts.bytes += view.byteLength;
      entry.counts.frames++;
    }
    return result;
  };
})();
