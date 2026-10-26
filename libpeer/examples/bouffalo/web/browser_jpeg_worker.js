import encode from "./vendor/jsquash-jpeg/encode.js";

self.onmessage = async ({ data: { image, quality } }) => {
  try {
    const bytes = await encode(image, {
      quality: Math.round(quality * 100),
      baseline: true,
      progressive: false,
      // Disabling optimization can produce invalid entropy data in this bundled encoder.
      optimize_coding: true,
      auto_subsample: false,
      chroma_subsample: 2,
      quant_table: 0,
    });
    self.postMessage({ bytes }, [bytes]);
  } catch (error) {
    self.postMessage({ error: error.message });
  }
};
