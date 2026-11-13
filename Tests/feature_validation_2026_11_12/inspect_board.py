"""Read-only extraction of the V4 PCB net assignments used by this change."""
from pathlib import Path
import re

root = Path(r'C:/Suraj J R Folder/Other consultancy projects/Enhanced Innovations Pvt. Ltd/Phloton Gen 4/Designs/Phloton_CB_V4/Phloton_CB_V4')
text = (root / 'Phloton_CB_V4.kicad_pcb').read_text(encoding='utf-8')
for footprint in re.split(r'\n\t\(footprint ', text)[1:]:
    if not any(n in footprint for n in ('LID_OPEN_DETECT', 'BUZZER_CTRL', 'STM32L451')):
        continue
    props = dict(re.findall(r'\(property "([^"]+)" "([^"]*)"', footprint))
    print(props.get('Reference'), props.get('Value'))
    for pad in re.split(r'\n\t\t\(pad ', footprint)[1:]:
        net = re.search(r'\(net "([^"]+)"\)', pad)
        pin = re.search(r'\(pinfunction "([^"]+)"\)', pad)
        print('  pad', pad.splitlines()[0], 'pin', pin[1] if pin else '', 'net', net[1] if net else '')
