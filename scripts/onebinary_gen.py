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

json_files = []
header_files = []
for file in args.files:
  if file.endswith('.json'):
    json_files.append(file)
  elif file.endswith('h'):
    header_files.append(file)
  else:
    raise Exception("unknown file type: " + file)

# print('json')
# print(json_files)
# print('h')
# print(header_files)

MIST_IN = "MistIn"
MIST_OUT = "MistOut"
CAP_LINE = '    capabilities["{category}"]["{connector}"] = JSON::fromString({json_str});'

capabilities = []

for name in json_files:
  path = Path(name)
  stem = path.stem
  text = path.read_text().strip("\n")
  json_str = json.dumps(text)
  category = ''
  connector = ''
  class_name = ''
  if stem.startswith(MIST_IN):
    category = "inputs"
    connector = stem[len(MIST_IN):]
    class_name = "Mist::Input" + connector
    func_name = "InputMain"
  elif stem.startswith(MIST_OUT):
    category = "connectors"
    connector = stem[len(MIST_OUT):]
    class_name = "Mist::Out" + connector
    func_name = "OutputMain"
  else:
    raise Exception("unknown binary naming convention: " + stem)
  capabilities.append({
    'json_str': json_str,
    'category': category,
    'connector': connector,
    'class_name': class_name,
    'binary_name' : stem,
    'func_name': func_name,
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

entrypoint_lines = []

for header_file in header_files:
  entrypoint_lines.append('#include "' + header_file + '"')

entrypoint_lines.extend([
  '#include <mist/config.h>',
  '#include <mist/defines.h>',
  '#include <mist/socket.h>',
  '#include <mist/util.h>',
  '#include <mist/stream.h>',
  '#include "src/session.h"',
  '#include "src/controller/controller.h"',
  '#include "src/output/mist_out.cpp"',
  '#include "src/input/mist_in.cpp"',
])


entrypoint_lines.extend([
  # '#include "src/output/mist_out.cpp"',
  # '#include "src/input/mist_in.cpp"',
  # '#include "src/session.cpp"',
  # '#include "src/controller/controller.cpp"',
  'int main(int argc, char *argv[]){',
  '  if (argc < 2) {',
  '    program_invocation_short_name = (char *)"MistController";'
  '    return ControllerMain(argc, argv);',
  '  }',
  '  // Create a new argv array without argv[1]',
  '  int new_argc = argc - 1;',
  '  char** new_argv = new char*[new_argc];',
  '  for (int i = 0, j = 0; i < argc; ++i) {',
  '      if (i != 1) {',
  '          new_argv[j++] = argv[i];',
  '      }',
  '  }',
  '  if (strcmp(argv[1], "MistController") == 0) {',
  '    return ControllerMain(new_argc, new_argv);',
  '  }',
])

for cap in capabilities:
  entrypoint_lines.extend([
  '  else if (strcmp(argv[1], "' + cap['binary_name'] + '") == 0) {',
  '    program_invocation_short_name = argv[1];'
  '    return ' + cap['func_name'] +'<' + cap['class_name'] + '>(new_argc, new_argv);',
  '  }',
  ])

entrypoint_lines.extend([
  '  else if (strcmp(argv[1], "MistSession") == 0) {',
  '    return SessionMain(new_argc, new_argv);',
  '  }',
  '  else {',
  '    program_invocation_short_name = (char *)"MistController";',
  '    return ControllerMain(argc, argv);',
  '  }',
  '  INFO_MSG("binary not found: %s", argv[1]);',
  '  return 202;',
  '}',
])

entrypoint_fullpath = os.path.join(os.getcwd(), args.entrypoint)
Path(entrypoint_fullpath).write_text('\n'.join(entrypoint_lines))
