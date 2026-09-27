# Translation

At the present time the main way of performing translations is to open a PR to this repository.
All translations are stored as `json` files in the repository. Currently there is ongoing work to look into a more user friendly method of editing translations than these but for now these are reliable.

You can create a pull request with the new / updated json configuration file, and this will include this language into the new builds for the firmware.

For testing you can build locally and test of course; but if you dont want to figure out the build environment; you can just open a PR and github will build the firmware for you using the _actions_ feature.

This means that once you have a github account you can perform all of your edits inside Github should this be desired.

Translations are _NOT_ accepted via issues/discussions or email.

## FNIRSI HS-02 colour screens

The HS-02 home, soldering and sleep screens show four short labels in a small
pixel font of their own. They come from the optional `gaugeLabels` section of
the translation file:

```json
"gaugeLabels": {
  "Ready": "READY",
  "Set": "SET",
  "Boost": "BOOST",
  "Sleep": "SLEEP"
}
```

Use uppercase and keep each label short (about 8 letters). Any label left out
falls back to English. So does a label using a character the font subset does
not have: it covers Latin, Greek and Cyrillic, but not CJK yet.

To see how a translation fits the HS-02 screen without the hardware, run
`tools/hs02-lang-preview/run.sh <LANG>`. It renders every screen to PNG.
