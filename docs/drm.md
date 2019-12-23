Some notes on the drm backend
=============================

## The cursor problem

When the pointer is moved we want to automatically update the cursor
positions. We can't just commit a new atomic state though since there
may be a pageflip pending and even if not, by doing so we might block
the pageflip triggered by the next rendered frame (for the primary plane).
It might be possible to handle both cases somewhat gracefully with some
guesswork (deferring updates) but this complicates the api significantly.
We therefore use the legacy `drmModeSetCursor`, `drmModeMoveCursor` for
now.
