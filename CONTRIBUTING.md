# Contributing

Use Ubuntu 22.04 or WSL2 Ubuntu 22.04. Before submitting a change run:

```bash
python3 scripts/verify_source_tree.py
make components
```

Changes affecting Kernel, DTB, RootFS, FIP or image packaging must also pass:

```bash
make release
make verify
```

Do not commit Wi-Fi credentials, private keys, logs, build output, host-tools
or release images.
