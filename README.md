# refil-zmk-behaviour-modules
 A few different behaviour modules for ZMK to do silly things

# How to use

Simply add the desired branches to your `zmk-config` repo's west.yml file like so:
```
- name: keysmash
    url: https://github.com/ReFil/refil-zmk-behaviour-modules
    revision: keysmash
- name: sarcasm
    url: https://github.com/ReFil/refil-zmk-behaviour-modules
    revision: sarcasm
```

Then include the appropriate behaviour header files in your .keymap file

```
#include <behaviors/keysmash.dtsi>
#include <behaviors/sarcasm.dtsi>

```

Then use the behaviours in the keymap

```
&keysmash
&sarcasm
```

Some behaviours may need additional configuration. This is usually documented in the `dts/bindings/behaviors/zmk,behavior-<behavior>.yaml` file 