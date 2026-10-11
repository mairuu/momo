# stb_image

Sean Barrett's image loader: PNG, JPEG and a few other formats, decoded into rows of pixels. Chapter 12 explains why momo needs one.

`stb_image.h` is version 2.30, from <https://github.com/nothings/stb>. The last commit that changed it is `013ac3beddff3dbffafd5177e7972067cd2b5083` (2024-05-31), and the file is byte for byte the one at that commit:

```
curl -O https://raw.githubusercontent.com/nothings/stb/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h
sha256sum stb_image.h
594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3  stb_image.h
```

`stb_image.c` is momo's: it compiles the library's code once, with the options its comment lists (PNG only, no `fopen`).

## License

stb_image is in the public domain, or under the MIT license, whichever you prefer. `LICENSE` is the license file from the same repository. The same text is at the end of `stb_image.h`.
