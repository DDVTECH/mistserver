#!/bin/bash

FILE_PATH="$1"
FILE_NAME="$2"

function main() {
  cd "${FILE_PATH}"

  if [[ "${GCLOUD_KEY:-}" == "" ]]; then
    echo "GCLOUD_KEY not found, not uploading to Google Cloud."
    exit 0
  fi

  # https://stackoverflow.com/a/44751929/990590
  BUCKET="${GCLOUD_BUCKET}"
  PROJECT="mistserver"
  BUCKET_PATH="${PROJECT}/${CI_COMMIT_SHA}/${FILE_NAME}"
  RESOURCE="/${BUCKET}/${BUCKET_PATH}"
  CONTENT_TYPE="application/x-compressed-tar"
  DATE="$(date -R)"

  stringToSign="PUT\n\n${CONTENT_TYPE}\n${DATE}\n${RESOURCE}"
  SIGNATURE="$(echo -en ${stringToSign} | openssl sha1 -hmac "${GCLOUD_SECRET}" -binary | base64)"
  FULL_URL="https://storage.googleapis.com${RESOURCE}"

  # Failsafe - don't overwrite existing uploads!
  if curl --head --fail "${FULL_URL}" 2>/dev/null; then
    echo "${FULL_URL} already exists, not overwriting!"
    exit 0
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
