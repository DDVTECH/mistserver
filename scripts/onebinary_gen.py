#!/usr/bin/env python

# Generates files necessary with hardcoded capabilities for Python

import os
from pathlib import Path
import json
import argparse

parser = argparse.ArgumentParser(
  prog='onebinary_gen.py',
  description='Generates header files necessary to create the MistServer combined binary'
)

parser.add_argument('--cap-header', help="location of the generated capabilities header", required=True)
parser.add_argument('--entrypoint', help="location of the generated entrypoint file", required=True)
parser.add_argument('files', metavar='N', type=str, nargs='+', help='input binary json files generated elsewhere')

args = parser.parse_args()

MIST_IN = "MistIn"
MIST_OUT = "MistOut"
CAP_LINE = '    capabilities["{category}"]["{connector}"] = JSON::fromString({json_str});'

capabilities = []

for name in args.files:
  path = Path(name)
  stem = path.stem
  text = path.read_text().strip("\n")
  json_str = json.dumps(text)
  category = ''
  connector = ''
  if stem.startswith(MIST_IN):
    category = "inputs"
    connector = stem[len(MIST_IN):]
  elif stem.startswith(MIST_OUT):
    category = "connectors"
    connector = stem[len(MIST_OUT):]
  else:
    raise Exception("unknown binary naming convention: " + stem)
  capabilities.append({
    'json_str': json_str,
    'category': category,
    'connector': connector,
  })

cap_lines = [
  '#include "src/controller/controller_capabilities_static.h"',
  'namespace Controller{',
  '  void addStaticCapabilities(JSON::Value &capabilities) {',
]

for cap in capabilities:
  line = CAP_LINE.format(**cap)
  cap_lines.append(line)

cap_lines.extend([
  '  }',
  '}',
])

out_fullpath = os.path.join(os.getcwd(), args.cap_header)
Path(out_fullpath).write_text('\n'.join(cap_lines))
