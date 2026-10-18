# Evaluation programs

Small programs written the way a person would, for `ovis_eval programs
eval/programs` (task M1.14): how many questions each asks, how each is
settled (rule, ranking shortcut, model, or left to the author), whether
the model's picks are the ones meant, and how long a build takes.

Each program says under its first line what its unclear spans mean:

    # expect "mean of scores" is average of scores
    # expect "roll a die" is none
    # expect "SPAN" is READING | OTHER READING

`none` means no reading fits, so the build should ask the author. A
program with nothing unclear says `# expect nothing unclear`.
