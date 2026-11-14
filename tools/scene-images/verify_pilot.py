"""Check pilot completeness/provenance without approving artistic fidelity."""
import hashlib
import json
from pathlib import Path
import struct
from PIL import Image

ROOT = Path(__file__).resolve().parents[2] / 'assets/source/white-house'
PROJECT = ROOT.parents[2]
REVIEW = PROJECT / 'assets/review/white-house'


def load(path):
    return json.loads(Path(path).read_text())


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    manifest = load(ROOT / 'manifest.json')
    records = manifest['records']
    phase1 = load(PROJECT / 'assets/source/art-direction/manifest.json')
    rooms = {r['room'] for r in records}
    expected = {(r['runtime_room'], s['runtime_state']) for r in phase1['rooms']
                if r['room'] in rooms for s in r['states']}
    actual = [(r['runtime_room'], r['runtime_state']) for r in records if r['binding'] == 'mapped']
    assert set(actual) == expected and len(actual) == len(expected)
    unbound = [r for r in records if r['binding'] == 'unbound-concept']
    assert len(unbound) == 1 and unbound[0]['room'] == 'ATTIC' and unbound[0]['runtime_state'] is None
    for record in records:
        assert record['status'] == 'generated'
        for field in ['original', 'metadata', 'request', 'rgb_preview', 'yjk_preview', 'kimg']:
            assert (ROOT / record[field]).is_file(), (record['asset_key'], field)
        for field in ['rgb_preview', 'yjk_preview']:
            with Image.open(ROOT / record[field]) as image:
                assert image.size == (256, 159)
        packed = (ROOT / record['kimg']).read_bytes()
        assert len(packed) == 40720 and packed[:4] == b'KIMG'
        assert struct.unpack_from('<HHI', packed, 8) == (256, 159, 40704)
    generation_metadata = []
    removed_references = 0
    for path in sorted((ROOT / 'metadata').glob('room-*.json')):
        meta = load(path)
        assert digest(ROOT / meta['request']) == meta['request_sha256']
        assert any(digest(path) == meta['source_sha256'] for path in (ROOT / 'originals').glob('*.png'))
        for ref in meta['references']:
            if ref.get('retained', True):
                assert digest(ROOT / ref['path']) == ref['sha256']
            else:
                assert ref['path'] is None and len(ref['sha256']) == 64
                removed_references += 1
        generation_metadata.append(path.name)
    assert {p.name for p in (ROOT / 'originals').glob('*.png')} == {
        r['asset_key'] + '.png' for r in records}
    assert len(generation_metadata) == len(records)
    for filename in ['contact-sheet-originals.png', 'contact-sheet-rgb-256x159.png', 'contact-sheet-yjk-256x159.png']:
        with Image.open(REVIEW / filename) as image:
            image.verify()
    review = {
        'room-180-state-1': 'Classic mailbox revision complete; production continuity review still required.',
        'room-180-state-2': 'Path and mailbox preserved; minor repainting outside intended change.',
        'room-81-state-1': 'Roof/elevation continuity requires review.',
        'room-80-state-1': 'Roof/elevation continuity requires review.',
        'room-79-state-1': 'Rear building proportions require review.',
        'room-79-state-2': 'Corrected: window fully open on the same wall, chimney and environment visually preserved. Cross-view house proportions still require review.',
        'room-203-state-1': 'Downward chimney and western passage not clearly represented.',
        'room-203-state-2': 'Open/ajar distinction inadequate; unintended lighting change.',
        'room-201-state-unbound': 'Unbound concept; unsupported unboarded attic window.'}
    for record in records:
        record.update(deployment_ready=False, review=review[record['asset_key']])
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    report = {
        'schema': 'woh_phase2_pilot_acceptance_v1', 'artifact_checks': 'PASS',
        'mapped_images': len(actual), 'unbound_concepts': len(unbound),
        'high_resolution_originals_retained': len(list((ROOT / 'originals').glob('*.png'))),
        'generation_records_verified': len(generation_metadata),
        'superseded_reference_images_removed': removed_references,
        'preview_dimensions': [256, 159], 'kimg_files_verified': len(records),
        'runtime_pairs_exactly_match_phase1': True,
        'historical_phase2_snapshot': 'assets/review/white-house/metadata/protected-input-hashes.json',
        'reference_and_request_hashes_verified': True,
        'source_aspect_exceptions': [r['asset_key'] for r in records
                                    if abs(r['source_size'][0] / r['source_size'][1] - 4/3) > 0.05],
        'visual_review': 'Requires changes before production; see EVALUATION.md.',
        'hardware_display_test': 'Not performed.'}
    (REVIEW / 'metadata/acceptance.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
