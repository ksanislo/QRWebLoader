# QR Web Loader

Usage:
Point CAM1 (rear facing, right side) at a QR code containing the URL of the .cia you would like to install. Usually about 9" away works well for a QR of normal size, but your device may differ.

You can use both regular http/https links, as well as direct .cia file links on Mega.nz.

Compiling this requires my fork of [citrus](https://github.com/ksanislo/citrus/) which provides an overloaded version of ctr::app::install() that implements callbacks, [quirc](https://github.com/dlbeer/quirc) for QR decoding, [jsmn](http://zserge.com/jsmn.html) for JSON, and [mbedTLS](https://tls.mbed.org) for AES/base64.
