# AFF4 -The Advanced Forensics File Format

The Advanced Forensics File format 4 was originally designed and published in
"Extending the advanced forensic format to accommodate multiple data sources,
logical evidence, arbitrary information and forensic workflow" M.I. Cohen,
Simson Garfinkel and Bradley Schatz, digital investigation 6 (2009) S57–S68.

The format is an open source format used for the storage of digital evidence and
data.

The original paper was released with an earlier implementation written in
python. This project is a complete open source re-implementation for a general
purpose AFF4 library.

## What is currently supported.

Not all features described in the paper are currently supported:

1. Writing and Reading ZipFile style volumes.
2. Writing and Reading AFF4 Image streams using the deflate or snappy compressor.
3. Writing and reading RDF metadata using both YAML and Turtle.

What is not yet supported:

1. Encrypted AFF4 volumes.
2. Persistent data store.
3. HTTP backed streams.
4. Splitting an AFF4 Image across multiple volumes.
5. Map streams.
6. Support for signed statements or Bill of Materials.
7. Logical file acquisition.

# Notice

This is not an official Google product (experimental or otherwise), it is just
code that happens to be owned by Google.