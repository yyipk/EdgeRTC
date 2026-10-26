(() => {
  "use strict";

  const VIDEO_FPS = 30;
  const JPEG_MAX = 60 * 1024;
  const BACKLOG_MAX = 32 * 1024;
  const BUFFER_MAX = 96 * 1024;
  const JPEG_WORKER_URL = new URL("browser_jpeg_worker.js", document.currentScript.src);

  function compatibleJpeg(bytes) {
    if (bytes[0] !== 0xff || bytes[1] !== 0xd8) return false;
    let pos = 2;
    let baseline = false;
    while (pos + 4 <= bytes.length) {
      if (bytes[pos++] !== 0xff) return false;
      while (bytes[pos] === 0xff) pos++;
      const marker = bytes[pos++];
      const length = (bytes[pos] << 8) | bytes[pos + 1];
      if (marker === 0xdd || marker === 0xd9 || length < 2 || pos + length > bytes.length) return false;
      if (marker === 0xda) return baseline;
      if (marker === 0xc0) {
        baseline = length === 17 && bytes[pos + 2] === 8 &&
          ((bytes[pos + 3] << 8) | bytes[pos + 4]) === 480 &&
          ((bytes[pos + 5] << 8) | bytes[pos + 6]) === 480 &&
          bytes[pos + 7] === 3 && bytes[pos + 9] === 0x22 &&
          bytes[pos + 12] === 0x11 && bytes[pos + 15] === 0x11;
      }
      pos += length;
    }
    return false;
  }

  window.attachBrowserVideo = function (channel, anchor, report) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "media-control call-control";
    button.dataset.control = "camera";
    button.innerHTML = `<span class="control-icon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3" y="6" width="12" height="12" rx="2"/><path d="m15 10 6-3v10l-6-3"/><path class="camera-slash" d="m3 3 18 18"/></svg></span><span class="control-label">摄像头</span>`;
    const slash = button.querySelector(".camera-slash");
    anchor.insertAdjacentElement("afterend", button);

    const video = document.createElement("video");
    video.muted = true;
    video.playsInline = true;
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = 480;
    const context = canvas.getContext("2d", { alpha: false });
    let stream = null;
    let pending = false;
    let timer = null;
    let generation = 0;
    let encodingGeneration = null;
    let closed = false;
    let fallback = false;
    let encoder = null;
    let encodeRequest = null;

    const current = token => !closed && token === generation && channel.readyState === "open";

    function updateButton() {
      const active = !!stream && !pending;
      button.classList.toggle("is-muted", !active);
      button.classList.toggle("is-pending", pending);
      button.disabled = closed || pending || channel.readyState !== "open";
      button.title = pending ? "正在开启摄像头" : active ? "关闭摄像头" : "开启摄像头";
      button.setAttribute("aria-label", button.title);
      button.setAttribute("aria-pressed", String(active));
      button.querySelector(".control-label").textContent = pending ? "正在开启" : active ? "摄像头已开" : "摄像头已关";
      slash.style.display = active ? "none" : "";
    }

    function stop(message) {
      generation++;
      clearInterval(timer);
      timer = null;
      if (encoder) encoder.terminate();
      encoder = null;
      if (encodeRequest) encodeRequest.reject(new Error("Camera stopped"));
      encodeRequest = null;
      if (stream) stream.getTracks().forEach(track => track.stop());
      stream = null;
      video.pause();
      video.srcObject = null;
      pending = false;
      updateButton();
      if (message) report(message);
    }

    function encodeFallback(quality) {
      if (!encoder) {
        encoder = new Worker(JPEG_WORKER_URL, { type: "module" });
        encoder.onmessage = ({ data }) => {
          const request = encodeRequest;
          encodeRequest = null;
          if (!request) return;
          if (data.error) request.reject(new Error(data.error));
          else request.resolve(new Uint8Array(data.bytes));
        };
        encoder.onerror = event => {
          if (encodeRequest) encodeRequest.reject(new Error(event.message || "JPEG encoder failed to load"));
          encodeRequest = null;
        };
      }
      const image = context.getImageData(0, 0, 480, 480);
      return new Promise((resolve, reject) => {
        encodeRequest = { resolve, reject };
        encoder.postMessage({ image, quality }, [image.data.buffer]);
      });
    }

    async function capture(token) {
      if (!current(token) || !stream || encodingGeneration !== null ||
          video.readyState < 2 || !video.videoWidth || channel.bufferedAmount > BACKLOG_MAX) return;
      encodingGeneration = token;
      try {
        const side = Math.min(video.videoWidth, video.videoHeight);
        context.drawImage(video, (video.videoWidth - side) / 2,
          (video.videoHeight - side) / 2, side, side, 0, 0, 480, 480);
        let bytes;
        for (const quality of [0.65, 0.45, 0.25]) {
          if (fallback) {
            bytes = await encodeFallback(quality);
          } else {
            const blob = await new Promise(resolve => canvas.toBlob(resolve, "image/jpeg", quality));
            if (!current(token)) return;
            if (!blob) throw new Error("JPEG encoding failed; try restarting the camera");
            bytes = new Uint8Array(await blob.arrayBuffer());
            if (!current(token)) return;
            // Safari's restart interval follows SOF and is unsupported by the device parser.
            if (!compatibleJpeg(bytes)) {
              fallback = true;
              bytes = await encodeFallback(quality);
            }
          }
          if (!current(token)) return;
          if (bytes.length <= JPEG_MAX) break;
        }
        if (bytes.length > JPEG_MAX) {
          report("Camera frame exceeds 60 KiB and was dropped; reduce scene detail or camera noise.");
          return;
        }
        if (!compatibleJpeg(bytes)) {
          throw new Error("JPEG encoder produced a format unsupported by the LCD");
        }
        if (channel.bufferedAmount > BACKLOG_MAX || channel.bufferedAmount + bytes.length > BUFFER_MAX) return;
        channel.send(bytes);
      } catch (error) {
        if (token === generation && !closed) {
          stop(`Camera stopped: ${error.message}. If the connection closed, reconnect before retrying.`);
        }
      } finally {
        if (encodingGeneration === token) encodingGeneration = null;
      }
    }

    async function start() {
      if (closed || pending || stream || channel.readyState !== "open") return;
      const token = ++generation;
      pending = true;
      updateButton();
      report("Opening camera; allow camera access to send video to the device.");
      try {
        if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
          throw new Error("Camera access requires HTTPS or localhost and a supported browser");
        }
        const media = await navigator.mediaDevices.getUserMedia({
          audio: false,
          video: { facingMode: { ideal: "user" }, width: { ideal: 640 }, height: { ideal: 480 }, frameRate: { ideal: VIDEO_FPS, max: VIDEO_FPS } }
        });
        if (!current(token)) {
          media.getTracks().forEach(track => track.stop());
          return;
        }
        stream = media;
        video.srcObject = media;
        await video.play();
        if (!current(token)) return;
        pending = false;
        updateButton();
        report(`Camera sending 480x480 JPEG to the device, up to ${VIDEO_FPS} fps.`);
        timer = setInterval(() => capture(token), 1000 / VIDEO_FPS);
        capture(token);
      } catch (error) {
        if (token === generation && !closed) {
          stop(`Cannot start camera: ${error.message}. Check camera permission and that another app is not using it.`);
        }
      }
    }

    function clicked() {
      if (closed || pending || channel.readyState !== "open") return;
      if (stream) stop("Camera stopped.");
      else start();
    }

    function opened() {
      start();
    }

    function dispose() {
      if (closed) return;
      closed = true;
      stop();
      button.removeEventListener("click", clicked);
      channel.removeEventListener("open", opened);
      channel.removeEventListener("closing", dispose);
      channel.removeEventListener("close", dispose);
      channel.removeEventListener("error", dispose);
      window.removeEventListener("pagehide", dispose);
      window.removeEventListener("offline", dispose);
      button.remove();
    }

    button.addEventListener("click", clicked);
    channel.addEventListener("open", opened);
    channel.addEventListener("closing", dispose);
    channel.addEventListener("close", dispose);
    channel.addEventListener("error", dispose);
    window.addEventListener("pagehide", dispose);
    window.addEventListener("offline", dispose);
    updateButton();
    if (channel.readyState === "open") opened();
    return dispose;
  };
})();
