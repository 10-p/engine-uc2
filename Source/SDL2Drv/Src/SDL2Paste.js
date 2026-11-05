// SDL2Paste.js: the page's paste events for SDL2Drv on the web (SDL2Client.cpp), linked with --js-library
// (Source/SDLLaunch/cmake/Web.cmake).
//
// A paste event hands the page the clipboard's text (clipboardData) without the permission prompt that
// navigator.clipboard.readText() may show. The listener runs on the page's thread and hands the text to the engine's
// thread as Emscripten's own input listeners hand their events over (libhtml5.js); it is one of
// JSEvents.eventHandlers, so whatever removes those (the page stopping the engine's input) removes it too.

addToLibrary({
  uc2_web_listen_paste__proxy: 'sync',
  uc2_web_listen_paste__deps: ['$JSEvents', '$lengthBytesUTF8', '$stringToUTF8', 'malloc', 'free',
    '_emscripten_run_callback_on_thread'],
  uc2_web_listen_paste: (callback, thread, maxChars) => {
    // A paste into a field of the page's own (the launcher's, the ini editor) is that field's.
    var editable = (t) => !!t && (t.isContentEditable === true || /^(input|textarea|select)$/i.test(t.tagName || ''));
    var onPaste = (e) => {
      if (editable(e.target)) return;
      // At most 2 x maxChars UTF-16 units: the engine types maxChars characters at most, each one or two units.
      var text = e.clipboardData ? e.clipboardData.getData('text/plain').slice(0, 2 * maxChars) : '';
      if (!text) return;
      var size = lengthBytesUTF8(text) + 1;
      var buffer = _malloc(size);
      stringToUTF8(text, buffer, size);
      // The engine's thread gets a copy of the bytes, freed there after the callback.
      __emscripten_run_callback_on_thread(thread, callback, 0, buffer, size, 0);
      _free(buffer);
    };
    JSEvents.registerOrRemoveHandler({
      target: document,
      eventTypeString: 'paste',
      callbackfunc: callback,
      handlerFunc: onPaste,
      useCapture: false,
    });
  },
});
