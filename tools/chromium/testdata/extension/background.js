chrome.runtime.onInstalled.addListener(() => {
  console.log("CharmOS extension installed");
});

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message === "charmos-extension-ping") {
    sendResponse("charmos-service-worker-active");
  }
});
