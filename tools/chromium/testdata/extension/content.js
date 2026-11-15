chrome.runtime.sendMessage("charmos-extension-ping", response => {
  if (response !== "charmos-service-worker-active") {
    return;
  }
  const marker = document.createElement("div");
  marker.id = "charmos-extension-result";
  marker.textContent = "CharmOS extension and service worker: active";
  marker.style.cssText = "padding:12px;background:#dff6dd;color:#183a1d;font:16px sans-serif";
  document.body.prepend(marker);
  document.title = "CharmOS extension active";
});
