#!/usr/bin/env python3
"""Package the separate, unconfirmed CS anti-ghost experiment; never flash."""
from pathlib import Path
import hashlib
import json
import runpy
import shutil
import subprocess

root=Path(__file__).resolve().parents[1]
base=root/'build/install/splash-heatmap-v1.5.0-r12'
out=root/'build/install/splash-heatmap-v1.5.0-r12-deghost-test'
original=json.loads((base/'manifest.json').read_text())
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
assert original['custom_version']=='v1.5.0-splash12'
assert sha(base/'Wireless_neuron.hex')==original['files_sha256']['Wireless_neuron.hex']
assert sha(base/'keyscanner.bin')==original['files_sha256']['keyscanner.bin']
# Reuse the exact verified Neuron artifact, never relabel an unbuilt main image.
for name,digest in original['source_sha256'].items():
    assert sha(root/name)==digest, 'Base r12 source changed: '+name
patcher=runpy.run_path(str(root/'side-patches/tools/patch-side-deghost.py'))
stock=(root/'build/vendor/keyscanner-v1.5.0.bin').read_bytes()
side=patcher['patch'](stock)
log=(root/'build/verification/side-driver-tests.log').read_text()
assert 'ordinary SPI 52 02 B3; experiment SPI 52 02 B1; CS pull-up selection only' in log
assert 'Deghost experiment: exact image/hash/CRC/boundaries, map regression and rejected inputs passed' in log
metadata=json.loads((root/'side-patches/patches/raise2-v1.5.0-deghost-experiment.json').read_text())
out.mkdir(parents=True,exist_ok=True)
shutil.copyfile(base/'Wireless_neuron.hex',out/'Wireless_neuron.hex')
(out/'keyscanner.bin').write_bytes(side)
shutil.copyfile(root/'docs/tail-driver-investigation.md',out/'README.txt')
shutil.copyfile(root/'docs/input-profile-loss.md',out/'input-profile-loss.md')
hashes={name:sha(out/name) for name in ['Wireless_neuron.hex','keyscanner.bin']}
manifest={'experimental':True,'physical_red_tail_fix_confirmed':False,
          'main_neuron_identical_to_r12':True,'custom_version':'v1.5.0-splash12',
          'main_neuron_source_commit':original['source_commit'],
          'experiment_source_commit':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
          'side_patch_commit':subprocess.check_output(['git','-C',str(root/'side-patches'),'rev-parse','HEAD'],text=True).strip(),
          'requires_different_side_image':True,'side_identical_to_revision_2':False,
          'hardware_tested':False,'hardware_recovery_tested':False,
          'ordinary_r12_side_sha256':original['files_sha256']['keyscanner.bin'],
          'side_metadata':metadata,'files_sha256':hashes,
          'green_tail_timing_unchanged_from_r12':True,
          'does_not_add_sub_minimum_pwm_levels':True,
          'rollback_folder':str(base)}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(out/'SHA256SUMS').write_text(''.join(digest+'  '+name+'\n' for name,digest in hashes.items()))
print('Prepared separate unconfirmed deghost test:',out)
print('Same Neuron as r12; DIFFERENT side image. No keyboard was flashed.')
