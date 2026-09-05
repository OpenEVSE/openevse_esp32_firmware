"""Re-encode built GUI assets so they take less flash.

The GUI build hands us assets that are already gzipped, and PNG icons saved as
8-bit RGBA. Neither is as small as it could be, and on the 4MB boards
(`openevse_wifi_v1`, a 1,966,080-byte app partition) the embedded GUI is the
single largest thing in the image, so the difference decides whether the build
fits.

Two independent passes, applied when the headers under src/web_static are
regenerated from a GUI dist directory:

- Text assets are decompressed and re-compressed. The default is zopfli, which
  emits an ordinary gzip stream -- nothing on the client or in
  web_server_static.cpp has to change, it is simply a better-packed deflate.
  Setting WEB_ASSET_ENCODING=brotli switches to brotli, which is a further
  ~11% smaller but changes the Content-Encoding on the wire (see below).
- PNG icons are quantized to a 256-entry palette. The icon artwork only uses
  431-457 distinct colours, so 256 is visually indistinguishable (RMSE ~0.0007)
  while dropping the 512px icons from ~15KB to ~5KB.

Every pass keeps its result only if it actually came out smaller, so a future
GUI build that already ships optimal assets degrades to a no-op rather than
regressing.

A note on brotli: web_server_static.cpp sends Content-Encoding unconditionally
and never reads Accept-Encoding, so the encoding is fixed when these headers
are generated rather than negotiated per request. There is no affordable
runtime fallback -- decoding brotli on the device would need RFC 7932's
122,784-byte static dictionary plus a decoder, far more than the ~35KB it
saves. So brotli must be verified against real browsers over plain HTTP (the
usual way this device is reached) before an env opts into it.
"""

import gzip
import io
import os

GZIP = "gzip"
BROTLI = "br"

_warned = set()


def _warn(message):
    if message not in _warned:
        _warned.add(message)
        print("web_assets: " + message)


def selected_encoding():
    """Encoding to use for text assets, honouring WEB_ASSET_ENCODING.

    Resolved against what is actually installed so the bytes written by
    optimise() and the Content-Encoding recorded in the static file table can
    never disagree.
    """
    requested = os.environ.get("WEB_ASSET_ENCODING", GZIP).strip().lower()
    if requested in ("br", "brotli"):
        try:
            import brotli  # noqa: F401
            return BROTLI
        except ImportError:
            _warn("brotli requested but the module is not installed, using gzip")
    elif requested not in (GZIP, "gz"):
        _warn("unknown WEB_ASSET_ENCODING %r, using gzip" % requested)
    return GZIP


def _compress(raw, encoding):
    if encoding == BROTLI:
        import brotli
        return brotli.compress(raw, quality=11)
    try:
        import zopfli.gzip
        return zopfli.gzip.compress(raw)
    except ImportError:
        _warn("zopfli is not installed, falling back to stdlib gzip -9")
        return gzip.compress(raw, 9)


def _recompress_gzip(data, encoding):
    try:
        raw = gzip.decompress(data)
    except (OSError, EOFError) as e:
        _warn("could not decompress a .gz asset (%s), leaving it as-is" % e)
        return data
    return _compress(raw, encoding)


def _requantize_png(data):
    try:
        from PIL import Image
    except ImportError:
        _warn("Pillow is not installed, PNG icons will not be optimised")
        return data
    try:
        image = Image.open(io.BytesIO(data))
        # Quantizing a palette image again would only lose colours for nothing.
        if image.mode == "P":
            return data
        rgba = image.convert("RGBA")
        # MEDIANCUT and MAXCOVERAGE reject RGBA. libimagequant gives the best
        # palette when Pillow was built with it; fast octree is always there.
        methods = [Image.Quantize.FASTOCTREE]
        if hasattr(Image.Quantize, "LIBIMAGEQUANT"):
            methods.insert(0, Image.Quantize.LIBIMAGEQUANT)
        for method in methods:
            try:
                palettized = rgba.quantize(colors=256, method=method)
            except ValueError:
                continue  # Pillow built without this quantizer
            out = io.BytesIO()
            palettized.save(out, format="PNG", optimize=True)
            return out.getvalue()
        return data
    except Exception as e:  # a malformed or exotic PNG must not break the build
        _warn("could not optimise a PNG (%s), leaving it as-is" % e)
        return data


def optimise(source_file, encoding=None):
    """Return the bytes to embed for source_file, re-encoded where worthwhile."""
    with open(source_file, "rb") as fh:
        data = fh.read()

    if source_file.endswith(".gz"):
        candidate = _recompress_gzip(data, encoding or selected_encoding())
    elif source_file.endswith(".png"):
        candidate = _requantize_png(data)
    else:
        return data

    if len(candidate) < len(data):
        print("web_assets: %s %d -> %d bytes (-%d)"
              % (os.path.basename(source_file), len(data), len(candidate),
                 len(data) - len(candidate)))
        return candidate
    return data
