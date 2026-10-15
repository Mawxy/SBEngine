"""Assemble the GUI: embed the WebAssembly engine and the HaviEngine bundles into the template.

usage: python gui/build.py <sbengine.wasm> <arena-dir> <out.html> [--standalone]
"""
import base64, pathlib, sys

here = pathlib.Path(__file__).parent
wasm = pathlib.Path(sys.argv[1]).read_bytes()
arena = pathlib.Path(sys.argv[2])
out = pathlib.Path(sys.argv[3])
standalone = "--standalone" in sys.argv

# Two adapters (old 3a45a1c bot, current bot), each wrapped so their helpers don't collide.
pack = ""
for name, f in (("AdapterOld", "havi_adapter.js"), ("AdapterNew", "havi_adapter_v2.js")):
    pack += (f"\nconst {name} = (function () {{\n" + (arena / f).read_text(encoding="utf-8")
             + "\nreturn { haviChoose, haviInit: typeof haviInit === 'function' ? haviInit : null };\n})();\n")
for name, f in (("HaviCurrent", "havi-bundle-5d092f9.js"), ("HaviShipped", "havi-bundle.js"), ("HaviFixed", "havi-bundle-signfix.js")):
    pack += f"\nconst {name} = (function () {{\n" + (arena / f).read_text(encoding="utf-8") + "\nreturn HaviEngine;\n})();\n"
assert "</script" not in pack

page = (here / "index.template.html").read_text(encoding="utf-8")
page = page.replace("__WASM_B64__", base64.b64encode(wasm).decode()).replace("__HAVI_PACK__", pack)
if standalone:
    page = ('<!doctype html>\n<html lang="en">\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n') + page
out.write_text(page, encoding="utf-8")
print(f"wrote {out} ({len(page) // 1024} KB)")
