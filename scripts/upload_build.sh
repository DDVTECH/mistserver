#!/bin/bash

PLATFORM="$1"
ARCH="$2"
BRANCH_NAME="$3"
CI_PATH="$4"
TAR_FILE="livepeer-mistserver-${PLATFORM}-${ARCH}.tar.gz"

function main() {
  cd "${CI_PATH}/bin"

  if [[ "${GCLOUD_KEY:-}" == "" ]]; then
    echo "GCLOUD_KEY not found, not uploading to Google Cloud."
    exit 0
  fi

  # https://stackoverflow.com/a/44751929/990590
  BUCKET="${GCLOUD_BUCKET}"
  PROJECT="mistserver"
  BUCKET_PATH="${PROJECT}/${BRANCH_NAME}/${CI_COMMIT_SHA}/${TAR_FILE}"
  RESOURCE="/${BUCKET}/${BUCKET_PATH}"
  contentType="application/x-compressed-tar"
  dateValue="$(date -R)"
  stringToSign="PUT\n\n${contentType}\n${dateValue}\n${RESOURCE}"
  SIGNATURE="$(echo -en ${stringToSign} | openssl sha1 -hmac "${GCLOUD_SECRET}" -binary | base64)"
  FULL_URL="https://storage.googleapis.com${RESOURCE}"

  # Failsafe - don't overwrite existing uploads!
  if curl --head --fail "${FULL_URL}" 2>/dev/null; then
    echo "${FULL_URL} already exists, not overwriting!"
    exit 0
  fi

  curl -X PUT -T "${TAR_FILE}" \
    -H "Host: storage.googleapis.com" \
    -H "Date: ${dateValue}" \
    -H "Content-Type: ${contentType}" \
    -H "Authorization: AWS ${GCLOUD_KEY}:${SIGNATURE}" \
    "${FULL_URL}"

  echo "Upload done!"
}

main
