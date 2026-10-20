# Collections written in Ovis, measured (generics.md G7)

`examples/collections` has a growing list and a hash table written in Ovis
on generics (§5 typed memory). These programs do the same work with them
and with the built-in list and map; `list_growing/growing.ovis` and
`map_table/table.ovis` are copies of the example's files.

    tools/peak_memory.py build/ovis eval/collections/*.ovis eval/collections/*/
    OVIS_MEASURE_FLAGS=--fast tools/peak_memory.py build/ovis …

The numbers are in docs/state.md ("Collections measured").
