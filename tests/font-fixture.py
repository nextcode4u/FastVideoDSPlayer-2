"""Expose the shipped NTFT assets to the host graphics test; output stays in tmp/."""
from pathlib import Path
out = Path("tmp/graphics-fonts")
out.mkdir(exist_ok=True)
parts = []
for name in ("RobotoRegular10", "RobotoMedium13"):
    data = Path("arm9/data", name + ".ntft").read_bytes()
    symbol = name + "_ntft"
    (out / (symbol + ".h")).write_text("extern const unsigned char " + symbol + "[];\n")
    parts.append("alignas(4) extern const unsigned char " + symbol + "[] = {" +
                 ",".join(str(byte) for byte in data) + "};\n")
(out / "fonts.cpp").write_text("".join(parts))
