﻿import sys
import os
import shutil
import subprocess
from pathlib import Path


engine_path = Path(__file__).resolve().parents[6]
assert (engine_path/'Binaries').is_dir()

build_dir = Path(sys.argv[1]).resolve()
# scripts_dir = sys.argv[2]
scripts_dir = (engine_path/'Restricted/NotForLicensees/Programs/UnrealEngineInstaller/UnrealEngineInstaller/Localization/Scripts').resolve()

msi_name = 'UnrealDatasmithMaxExporter.msi'

en_msi_dir = build_dir/'en-us'
en_msi_path = en_msi_dir/msi_name

assert en_msi_path.is_file(), f"Expected to see 'en-us' msi in {en_msi_path}"

master_msi_dir = build_dir/'Master'
master_msi_path = master_msi_dir/msi_name

master_msi_dir.mkdir(parents=True, exist_ok=True)

shutil.copy(en_msi_dir/msi_name, master_msi_path)

assert master_msi_path.is_file(), f"Expected to see master msi copied from root culture msi '{master_msi_path}'"

def add_language(culture_name, culture_code):
    cmd = [
        str(scripts_dir/'CreateAndEmbedLangTransform.bat'),
        str(en_msi_dir), msi_name, 
        culture_name, str(culture_code),
        str(master_msi_dir)]
    subprocess.check_call(cmd)

for l in 'de-de:1031, ko-kr:1042, es-es:1034, fr-fr:1036, ja-jp:1041, pt-pt:2070, zh-cn:2052'.split(', '):
	culture_name, culture_code = l.split(':')
	add_language(culture_name, culture_code)
