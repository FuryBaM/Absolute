# Absolute Chess

A playable console chess example written in Absolute.

Run it from the repository root:

```bat
examples\chess\run.bat
```

Enter coordinate moves such as `e2e4`, `g8f6`, or `e1g1`. Enter `q` to quit.

Implemented rules:

- legal movement and captures for every piece;
- alternating turns;
- check, checkmate, and stalemate detection;
- castling through unattacked squares;
- en passant;
- automatic pawn promotion to a queen.

Threefold repetition, the fifty-move rule, and choosing an underpromotion piece
are intentionally left out to keep the example readable.
