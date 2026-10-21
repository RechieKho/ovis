"""Generates N `to ...:` phrases as an Ovis program, for `ovis_eval questions --with`.

near: words the library and the eval cases use (count, total, items, largest...),
      as a utility package would export.
far:  words of unrelated domains (garden, guitar, rocket...).
Deterministic for a given (kind, n, seed).
"""
import random
import re
import sys

NEAR_VERBS = ["count", "total", "tally", "measure", "find", "pick", "take", "gather",
              "keep", "collect", "add up", "sum up", "work out", "figure", "score",
              "rank", "check", "split", "join", "trim", "list", "scan"]
NEAR_ADJ = ["biggest", "smallest", "first", "last", "middle", "average", "typical",
            "final", "top", "highest", "lowest", "whole", "odd", "even", "largest",
            "longest", "shortest", "next", "best", "usual", "grand", "running"]
NEAR_NOUNS = ["items", "numbers", "values", "entries", "scores", "prices", "words",
              "letters", "total", "count", "length", "part", "size", "sum", "mean",
              "elements", "amount", "figures", "digits", "names", "places", "spread"]
FAR_VERBS = ["water", "plant", "paint", "launch", "bake", "knit", "sail", "tune",
             "polish", "harvest", "brew", "fold", "stitch", "carve", "prune", "glaze",
             "dock", "hatch", "weave", "steer", "chop", "groom"]
FAR_ADJ = ["green", "muddy", "copper", "velvet", "stormy", "frozen", "golden", "rusty",
           "leafy", "salty", "woolen", "hollow", "crimson", "silky", "dusty", "tidal",
           "wooden", "smoky", "mossy", "brass", "sandy", "foggy"]
FAR_NOUNS = ["garden", "fence", "rocket", "bread", "scarf", "boat", "guitar", "kettle",
             "orchard", "harbour", "lantern", "saddle", "violin", "teapot", "anchor",
             "meadow", "chimney", "barrel", "compass", "quilt", "pottery", "hive"]

SLOTS = [("xs", "list of whole numbers"), ("t", "text"), ("n", "whole number"),
         ("ds", "list of decimals")]


def phrase(rng, verbs, adjs, nouns, index):
    verb, adj, noun = rng.choice(verbs), rng.choice(adjs), rng.choice(nouns)
    name, type_ = rng.choice(SLOTS)
    shape = rng.randrange(4)
    if shape == 0:
        header = f"the {adj} {noun} of ({name}: {type_})"
    elif shape == 1:
        header = f"{verb} the {noun} in ({name}: {type_})"
    elif shape == 2:
        header = f"{verb} the {adj} {noun} of ({name}: {type_})"
    else:
        name2, type2 = rng.choice([s for s in SLOTS if s[0] != name])
        header = f"{verb} ({name}: {type_}) with ({name2}: {type2})"
    return header, f"to {header}:\n  give back {index}\n"


def main():
    kind, count = sys.argv[1], int(sys.argv[2])
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    words = (NEAR_VERBS, NEAR_ADJ, NEAR_NOUNS) if kind == "near" else (
        FAR_VERBS, FAR_ADJ, FAR_NOUNS)
    rng = random.Random(f"{kind}-{seed}")
    seen, out = set(), []
    while len(out) < count:
        header, text = phrase(rng, *words, len(out) + 1)
        # One phrase per wording: two with the same words would ask.
        key = re.sub(r"\([^)]*\)", "_", header)
        if key in seen:
            continue
        seen.add(key)
        out.append(text)
    sys.stdout.write("\n".join(out))


main()
