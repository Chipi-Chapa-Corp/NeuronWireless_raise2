"""Permit only the disconnect callback's compiled text to differ from stock 1.5.0.

The baseline was extracted with GCC 10.3-2021.10 from the KeyboardManager.o
whose whole-object hash is recorded in stock-baseline.json. Section extraction
writes a separate temporary output; it never rewrites the input object.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

DISCONNECT = '.text._ZNSt17_Function_handlerIFvN23Communications_protocol6PacketEEZN12kaleidoscope6device5dygma13KeyboardHands5setupEvEUlRKS1_E0_E9_M_invokeERKSt9_Any_dataOS1_'


def verify(obj):
    expected = json.loads(Path(__file__).with_name('keyboard-manager-stock-sections.json').read_text())
    listing = subprocess.check_output(['arm-none-eabi-objdump', '-h', str(obj)], text=True)
    sections = re.findall(r'^\s*\d+\s+(\.text\S*)\s', listing, re.MULTILINE)
    assert set(sections) == set(expected), 'KeyboardManager text section set changed'
    actual = {}
    with tempfile.TemporaryDirectory(prefix='raise2-object-check-') as temp:
        output = Path(temp) / 'section.bin'
        for section in sections:
            subprocess.run(['arm-none-eabi-objcopy', '-O', 'binary',
                            '--only-section=' + section, str(obj), str(output)], check=True)
            actual[section] = hashlib.sha256(output.read_bytes()).hexdigest()
    changed = {name for name in sections if actual[name] != expected[name]}
    assert changed == {DISCONNECT}, ('Unexpected KeyboardManager code change', changed)
    return {
        'object_sha256': hashlib.sha256(Path(obj).read_bytes()).hexdigest(),
        'stock_text_sections_identical': len(sections) - 1,
        'changed_text_section': DISCONNECT,
        'changed_text_sha256': actual[DISCONNECT],
        'stock_text_sha256': expected[DISCONNECT],
    }
