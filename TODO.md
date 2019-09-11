# TODO

Tasks remaining. 

- [ ] Fix crash when `_DSXResult_BestPathWord` is called on recognition.
- [x] Fix long recognition hang when mic is first enabled.
- [x] Enable JIT Pausing on Windows.
- [ ] Pause until clients confirm their state on JIT Pause.
- [ ] Defer vocabulary modification (push word updates like grammars, only sync on pause).
- [ ] Unloading grammars (`"g.unload"` API call).
- [ ] Verify `"g.set"` API calls.
  - [ ] Unload grammar when `"g.set"` call is invalid.
- [ ] In `phrase` callbacks, use actual grammar names.

Please note, the bugs here are present on Dragon 13 - they haven't been
confirmed on newer versions yet.
