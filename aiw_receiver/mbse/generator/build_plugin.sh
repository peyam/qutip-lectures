#!/bin/bash
# Compile the headless model-builder plug-in against a Capella 7.0.x installation
# and install it into Capella's dropins folder.
#   CAPELLA_HOME=/opt/capella/capella ./build_plugin.sh
set -euo pipefail
CAPELLA_HOME=${CAPELLA_HOME:-/opt/capella/capella}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
P=$CAPELLA_HOME/plugins
( find "$P" -maxdepth 1 -name '*.jar'; find "$P" -mindepth 2 -maxdepth 3 -name '*.jar' ) | tr '\n' ':' > "$OUT/cp.txt"
printf -- '-cp\n%s\n' "$(cat "$OUT/cp.txt")" > "$OUT/args.txt"
javac -nowarn --release 17 @"$OUT/args.txt" -d "$OUT/bin" $(find "$HERE/src" -name '*.java')
mkdir -p "$OUT/bin/META-INF"
cp "$HERE/MANIFEST.MF" "$OUT/bin/META-INF/MANIFEST.MF"
cp "$HERE/plugin.xml" "$OUT/bin/"
rm -f "$CAPELLA_HOME"/dropins/aiw.capella.builder_*.jar
(cd "$OUT/bin" && jar cfm "$CAPELLA_HOME/dropins/aiw.capella.builder_1.0.0.jar" META-INF/MANIFEST.MF .)
rm -rf "$OUT"
echo "installed $CAPELLA_HOME/dropins/aiw.capella.builder_1.0.0.jar"
