# QR Web Loader

Usage:
Point CAM1 (rear facing, right side) at a QR code containing the URL of the .cia you would like to install. Usually about 9" away works well for a QR of normal size, but your device may differ.

The backend web server needs to respond to a direct GET request, so don't bother with Mega links. Content-Encoding: gzip (or deflate) can be supported if combiled with zlib support in citrus. 

This requires my fork of [citrus](https://github.com/ksanislo/citrus/) which provides an overloaded version of ctr::app::install() that can accept an httpcContext, and [quirc](https://github.com/dlbeer/quirc) for QR decoding. You'll also need a recent version of [ctrulib](https://github.com/smealum/ctrulib) with the updated httpcRecieveData().

