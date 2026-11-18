# WinShun Fluent components

The shared controls and Windows frame come from [WinShun at 865689a](https://github.com/LingCore/WinShun/tree/865689a52605381f9512b6a4bfab3f6050060643). The source is covered by the accompanying MIT license. Phosphor font files retain their [SIL Open Font License](third_party/phosphor/LICENSE).

The build embeds the Fluent look, shared button, input, toggle, title bar and their dependencies. The local copies of `Glyph.qml` and `themes/fluent/Look.qml` omit drawing paths and theme parts outside the shared Qt runtime contract. The tuning window takes the rest of its colors and appearance from WinShun's plugin appearance notification.
