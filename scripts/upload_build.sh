#!/bin/bash

function usage() {
  echo 1>&2 "Usage: $0 [-h] [-d <dir/to/file.name>] [-k <bucket/key>] [-f] [-r] <file.name>"
  exit 1
}

FORCE=0
BUCKET_ROOT=0
BUCKET_KEY="${CI_COMMIT_SHA}"
FILE_DIR="$PWD"

while getopts "hfrk:d:" arg; do
  case "$arg" in
  f)
    FORCE=1
    ;;
  d)
    FILE_DIR="${OPTARG}"
    [[ ! -d "${FILE_DIR}" ]] && echo 1>&2 "Directory '${FILE_DIR}' not found." && exit 127
    ;;
  k)
    BUCKET_KEY="${OPTARG}"
    ;;
  r)
    BUCKET_ROOT=1
    ;;
  h | *)
    usage
    ;;
  esac
done

shift $((OPTIND - 1))

FILE_NAME="$1"
[[ ! -f "${FILE_DIR}/${FILE_NAME}" ]] && echo 1>&2 "File not found at path: '${FILE_DIR}'." && exit 127

function main() {
  cd "${FILE_DIR}"

  if [[ "${GCLOUD_KEY:-}" == "" ]]; then
    echo 1>&2 "GCLOUD_KEY not found, not uploading to Google Cloud."
    exit 0
  fi

  # https://stackoverflow.com/a/44751929/990590
  BUCKET="${GCLOUD_BUCKET}"
  PROJECT="mistserver"
  if [[ "${BUCKET_ROOT}" -eq 0 ]]; then
    BUCKET_PATH="${PROJECT}/${BUCKET_KEY}/${FILE_NAME}"
  else
    BUCKET_PATH="${PROJECT}/${FILE_NAME}"
  fi
  RESOURCE="/${BUCKET}/${BUCKET_PATH}"
  CONTENT_TYPE="$(mimetype --brief "${FILE_DIR}/${FILE_NAME}")"
  DATE="$(date -R)"

  stringToSign="PUT\n\n${CONTENT_TYPE}\n${DATE}\n${RESOURCE}"
  SIGNATURE="$(echo -en ${stringToSign} | openssl sha1 -hmac "${GCLOUD_SECRET}" -binary | base64)"
  FULL_URL="https://storage.googleapis.com${RESOURCE}"

  if [[ "${FORCE}" -eq 0 ]]; then
    # Failsafe - don't overwrite existing uploads!
    if curl --head --fail "${FULL_URL}" 2>/dev/null; then
      echo "${FULL_URL} already exists, not overwriting!"
      exit 1
    fi
  fi

  curl --silent -X PUT -T "${FILE_NAME}" \
    -H "Host: storage.googleapis.com" \
    -H "Date: ${DATE}" \
    -H "Content-Type: ${CONTENT_TYPE}" \
    -H "Authorization: AWS ${GCLOUD_KEY}:${SIGNATURE}" \
    "${FULL_URL}"

  echo "Upload done!"
}

main
