#!/bin/bash
# Ranking-only sweep: for each case set, kind and size, put N generated phrases
# before every case and report where the right reading ranks.
#   sweep.sh OUT_DIR
set -u
here=$(cd "$(dirname "$0")" && pwd)
out=$(realpath -m "$1")
mkdir -p "$out"
cd "$here/../.."
echo "set kind n asked first shown mean_candidates mean_rank_found not_shown"
for set in questions hard-questions wording-questions; do
  for kind in near far; do
    for n in 0 10 25 50 100 200; do
      python3 "$here/distractors.py" $kind $n > "$out/$kind-$n.ovis"
      python3 "$here/prefix_cases.py" eval/$set.txt "$out/$kind-$n.ovis" "$out/$set-$kind-$n.txt"
      ./build/ovis_eval questions "$out/$set-$kind-$n.txt" --no-model > "$out/$set-$kind-$n.log" 2>&1
      python3 - "$out/$set-$kind-$n.log" "$set" "$kind" "$n" <<'EOF'
import re, sys
log, set_, kind, n = sys.argv[1:5]
text = open(log).read()
asked = re.search(r"(\d+) of (\d+) cases asked", text)
summary = re.search(r"first in (\d+), in the first question \(top 9\) in (\d+), of (\d+)", text)
ranks = re.findall(r"ranking: (#(\d+)|-) of (\d+)", text)
candidates = [int(c) for _, _, c in ranks]
found = [int(r) for _, r, _ in ranks if r]
missing = len(re.findall(r"ranking: (?:not shown|- of)", text))
if not asked or not summary:
    print(set_, kind, n, "ERROR", text[-300:].replace("\n", " | "))
else:
    print(set_, kind, n, f"{asked[1]}/{asked[2]}", summary[1], f"{summary[2]}/{summary[3]}",
          f"{sum(candidates)/max(1,len(candidates)):.1f}",
          f"{sum(found)/max(1,len(found)):.1f}", missing)
EOF
    done
  done
done
