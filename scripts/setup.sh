#!/usr/bin/env bash
# Clone ou met à jour nidmi-core comme dépendance locale de nidmi-player.
# À lancer une fois après git clone, ou pour mettre à jour nidmi-core.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
NIDMI_CORE_DIR="$REPO_DIR/../nidmi-core"
NIDMI_CORE_REMOTE="https://github.com/patricecolet/nidmi-core.git"

if [ -d "$NIDMI_CORE_DIR/.git" ]; then
  echo "nidmi-core: mise à jour (git pull)..."
  git -C "$NIDMI_CORE_DIR" pull --ff-only
elif [ ! -d "$NIDMI_CORE_DIR" ]; then
  echo "nidmi-core: clonage dans $NIDMI_CORE_DIR..."
  git clone "$NIDMI_CORE_REMOTE" "$NIDMI_CORE_DIR"
else
  echo "nidmi-core: dossier existant sans git, remplacement par un clone..."
  rm -rf "$NIDMI_CORE_DIR"
  git clone "$NIDMI_CORE_REMOTE" "$NIDMI_CORE_DIR"
fi

echo "OK: nidmi-core prêt. Lance 'pio run' pour compiler."
