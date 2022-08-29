#!/bin/bash

function checkDeps {
  command -v curl >/dev/null 2>&1 && return 0
  echo "ERROR: Please install npm" >&2
  return 1
}

checkDeps || return 1
npm run minimize

