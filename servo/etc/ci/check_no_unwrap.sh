#!/usr/bin/env bash
#
# Make sure listed files do not contain "unwrap"

set -o errexit
set -o nounset
set -o pipefail

cd "$(git rev-parse --show-toplevel)" # cd into repo root so make sure paths works in any case

# files that should not contain "unwrap"
FILES=("components/compositing/compositor.rs"
       "components/compositing/pipeline.rs"
       "components/constellation/constellation.rs")

! grep -n "unwrap(\|panic!(" "${FILES[@]}"
