"""Reject a held-story mapping if any known execution/callback module is far.

The SDK placer computes the call closure; this check also pins the intended
scope and indirect callbacks in the packaged application's placement report.
"""
import json
from pathlib import Path
import sys

required = {
    'compact.rel', 'compact_fetch.rel', 'memory.rel', 'variables.rel',
    'frames.rel', 'objects.rel', 'object_name.rel', 'text.rel', 'services.rel',
    'mapped_story.rel', 'game_output.rel', 'panel.rel',
}
report = json.loads(Path(sys.argv[1]).read_text())
zones = report['zones']
for member in sorted(required):
    found = [zone for path, zone in zones.items() if Path(path).name.split('_', 1)[-1] == member]
    if found != ['resident']:
        raise SystemExit(f'held story mapping requires {member} resident; got {found}')
print(f'VM residency: OK ({len(required)} execution/callback members outside aperture)')
