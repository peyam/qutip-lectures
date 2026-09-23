#!/bin/bash
# Regenerate the AIW-Rx Capella model, its diagrams and the validation report.
# Needs Capella 7.0.x, a JDK 17+ and Xvfb (xvfb-run) on Linux. Takes about 3 minutes.
#   CAPELLA_HOME=/opt/capella/capella ./generate.sh
set -euo pipefail
CAPELLA_HOME=${CAPELLA_HOME:-/opt/capella/capella}
HERE=$(cd "$(dirname "$0")" && pwd)
MBSE=$(dirname "$HERE")
WORK=$(mktemp -d)
"$HERE/build_plugin.sh"
cd "$CAPELLA_HOME"

# 1. Create the project, build the model, create and lay out the diagrams (plug-in "build" mode).
xvfb-run -a -s "-screen 0 2560x1600x24" ./capella -nosplash -clean -data "$WORK/build" --launcher.appendVmargs \
  -vmargs -Xmx4g -Daiw.mode=build -Daiw.log="$WORK/build.log"
cat "$WORK/build.log"
grep -q '^DONE' "$WORK/build.log"

# 2. Validate with Capella's own rules and export every diagram from a clean session.
CL="-nosplash -application org.polarsys.capella.core.commandline.core -data $WORK/check"
xvfb-run -a ./capella $CL -appid org.polarsys.capella.core.validation.commandline -import "$WORK/build/AIW-Rx" \
  -input "/AIW-Rx/AIW-Rx.aird" -outputfolder "/Report/validation" -forceoutputfoldercreation
xvfb-run -a -s "-screen 0 2560x1600x24" ./capella $CL -appid org.polarsys.capella.exportRepresentations \
  -input "/AIW-Rx/AIW-Rx.aird" -outputfolder "/Report/images" -forceoutputfoldercreation -imageFormat SVG

# 3. Copy results into the repository.
rm -rf "$MBSE/AIW-Rx" "$MBSE/diagrams" && mkdir -p "$MBSE/diagrams" "$MBSE/validation"
cp -r "$WORK/build/AIW-Rx" "$MBSE/AIW-Rx"
cp "$WORK"/check/Report/validation/AIW-Rx/AIW-Rx.aird/validation-results.html "$MBSE/validation/"
for f in "$WORK"/check/Report/images/AIW-Rx/AIW-Rx.aird/*.svg; do
  n=$(basename "$f" .svg | sed -E 's/[^A-Za-z0-9._-]+/_/g; s/^_|_$//g')
  cp "$f" "$MBSE/diagrams/$n.svg"
  command -v rsvg-convert >/dev/null && rsvg-convert -b white -o "$MBSE/diagrams/$n.png" "$f"
done
rm -rf "$WORK"
echo "model, diagrams and validation report written to $MBSE"
