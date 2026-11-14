"""Pilot acceptance: manifests, immutable artwork and portable binary reader."""
import contextlib
import copy
import ctypes as C
import io
from pathlib import Path
import subprocess
import tempfile
import unittest

from codec import decode, encode, hit
from generate import ROOT, arguments, generate, read, sha
from validate import validate_collection, validate_scene


class Table(C.Structure):
    _fields_=[('bytes',C.POINTER(C.c_uint8)),('size',C.c_uint16),('room',C.c_uint16),
              ('state',C.c_uint16),('count',C.c_uint8),('unbound',C.c_uint8)]


class Region(C.Structure):
    _fields_=[('width',C.c_uint16),('action_offset',C.c_uint16),('id',C.c_uint8),
              ('x',C.c_uint8),('y',C.c_uint8),('height',C.c_uint8),('action_count',C.c_uint8),
              ('label',C.POINTER(C.c_uint8)),('label_length',C.c_uint8)]


class HotspotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.output=Path(cls.temp.name)/'hotspots'
        cls.args=arguments(['--output',str(cls.output)])
        cls.images={p:sha(p) for p in cls.args.pilot.rglob('*.png')}
        with contextlib.redirect_stdout(io.StringIO()):generate(cls.args)
        cls.scenes={p.stem:read(p) for p in (cls.output/'scenes').glob('*.json')}
        library=Path(cls.temp.name)/'consumer.so'
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-shared','-fPIC',
                        str(Path(__file__).with_name('consumer.c')),'-o',str(library)],check=True)
        cls.lib=C.CDLL(str(library))
        cls.lib.whs_open.argtypes=[C.POINTER(Table),C.POINTER(C.c_uint8),C.c_uint16]
        cls.lib.whs_matches.argtypes=[C.POINTER(Table),C.c_uint16,C.c_uint16]
        cls.lib.whs_region.argtypes=[C.POINTER(Table),C.c_uint8,C.POINTER(Region)]
        cls.lib.whs_hit.argtypes=[C.POINTER(Table),C.c_uint16,C.c_uint16,C.POINTER(Region)]
        cls.lib.whs_action.argtypes=[C.POINTER(Table),C.POINTER(Region),C.c_uint8,
                                    C.c_void_p,C.c_uint8,C.c_void_p,C.c_uint8]

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def open_c(self,data):
        buffer=(C.c_uint8*len(data)).from_buffer_copy(data);table=Table()
        self.assertEqual(self.lib.whs_open(C.byref(table),buffer,len(data)),1)
        return buffer,table

    def test_complete_pilot_and_no_image_changes(self):
        report=validate_collection(self.args)
        self.assertEqual(report['errors'],[]);self.assertEqual(report['scenes'],9)
        self.assertEqual(sum(len(s['hotspots']) for s in self.scenes.values()),44)
        self.assertEqual(self.images,{p:sha(p) for p in self.images})
        self.assertFalse(list(self.output.rglob('*.kpr')))

    def test_deterministic_regeneration(self):
        before={p.relative_to(self.output):p.read_bytes() for p in self.output.rglob('*') if p.is_file()}
        with contextlib.redirect_stdout(io.StringIO()):generate(self.args)
        self.assertEqual(before,{p.relative_to(self.output):p.read_bytes() for p in self.output.rglob('*') if p.is_file()})

    def test_binary_round_trip_and_c_actions(self):
        for key,scene in self.scenes.items():
            data=(self.output/'binary'/(key+'.wht')).read_bytes();decoded=decode(data)
            self.assertEqual((decoded['room'],decoded['state']),(scene['room'],scene['state']))
            self.assertEqual(decoded['image_sha256'],scene['image']['sha256'])
            buffer,table=self.open_c(data)
            for i,spot in enumerate(scene['hotspots']):
                region=Region();self.assertTrue(self.lib.whs_region(C.byref(table),i,C.byref(region)))
                self.assertEqual([region.x,region.y,region.width,region.height],spot['rect'])
                self.assertEqual(C.string_at(region.label,region.label_length).decode(),spot['label'])
                for j,action in enumerate(spot['actions']):
                    label=C.create_string_buffer(9);command=C.create_string_buffer(81)
                    self.assertTrue(self.lib.whs_action(C.byref(table),C.byref(region),j,label,9,command,81))
                    self.assertEqual((label.value.decode(),command.value.decode()),(action['label'],action['command']))
                    self.assertFalse(self.lib.whs_action(C.byref(table),C.byref(region),j,label,1,command,81))

    def test_c_and_python_hit_testing_every_pixel(self):
        for key,scene in self.scenes.items():
            buffer,table=self.open_c(encode(scene));region=Region()
            for y in range(159):
                for x in range(256):
                    expected=hit(scene,x,y) if scene['state'] is not None else None
                    result=self.lib.whs_hit(C.byref(table),x,y,C.byref(region))
                    self.assertEqual(bool(result),expected is not None)
                    if expected:self.assertEqual(region.id,scene['hotspots'].index(expected)+1)

    def test_specific_object_wins_over_background(self):
        scene=self.scenes['room-180-state-1']
        self.assertEqual(hit(scene,202,135)['object_id'],'MAILBOX')
        self.assertEqual(hit(scene,170,115)['object_id'],'FRONT-DOOR')
        attic=self.scenes['room-201-state-unbound']
        self.assertEqual(hit(attic,210,115)['object_id'],'KNIFE')
        self.assertIsNone(hit(scene,256,159))

    def test_unbound_and_empty_binary_tables(self):
        scene=copy.deepcopy(self.scenes['room-201-state-unbound']);scene['hotspots']=[]
        self.assertIsNone(decode(encode(scene))['state'])
        buffer,table=self.open_c(encode(scene));region=Region()
        self.assertFalse(self.lib.whs_matches(C.byref(table),201,0))
        self.assertFalse(self.lib.whs_matches(C.byref(table),201,65535))
        self.assertFalse(self.lib.whs_hit(C.byref(table),100,100,C.byref(region)))
        bound=copy.deepcopy(self.scenes['room-180-state-1']);bound['hotspots']=[]
        buffer,table=self.open_c(encode(bound))
        self.assertTrue(self.lib.whs_matches(C.byref(table),180,1))
        self.assertFalse(self.lib.whs_matches(C.byref(table),79,1))

    def test_all_truncations_and_trailing_bytes_rejected(self):
        data=encode(self.scenes['room-180-state-1'])
        for n in range(len(data)):
            with self.assertRaises(ValueError):decode(data[:n])
            buffer=(C.c_uint8*len(data)).from_buffer_copy(data);table=Table()
            self.assertFalse(self.lib.whs_open(C.byref(table),buffer,n))
            self.assertFalse(table.bytes)
        with self.assertRaises(ValueError):decode(data+b'x')

    def test_corrupt_header_and_z80_width_overflow_rejected(self):
        original=encode(self.scenes['room-180-state-1'])
        for offset,value in [(0,0),(4,2),(5,255),(10,0),(11,0),(48,0)]:
            data=bytearray(original);data[offset]=value
            # Width 256 has low byte zero already; change the high byte case.
            if bytes(data)==original:continue
            with self.assertRaises(ValueError):decode(data)
            buffer=(C.c_uint8*len(data)).from_buffer_copy(data);table=Table()
            self.assertFalse(self.lib.whs_open(C.byref(table),buffer,len(data)))
        data=bytearray(original);data[51:53]=b'\xff\xff'
        with self.assertRaises(ValueError):decode(data)
        buffer=(C.c_uint8*len(data)).from_buffer_copy(data);table=Table()
        self.assertFalse(self.lib.whs_open(C.byref(table),buffer,len(data)))

    def test_bad_geometry_ids_menus_and_bindings_rejected(self):
        for change in [lambda s:s['hotspots'][0].update(rect=[255,158,2,2]),
                       lambda s:s['hotspots'][1].update(id=s['hotspots'][0]['id']),
                       lambda s:s['hotspots'][0].update(actions=[]),
                       lambda s:s['hotspots'][0]['actions'].append(copy.deepcopy(s['hotspots'][0]['actions'][0])),
                       lambda s:s['hotspots'][0]['actions'][0].update(command='open mailbox\nquit'),
                       lambda s:s['hotspots'][0].update(object_id='INVENTED-OBJECT'),
                       lambda s:s.update(state=999),
                       lambda s:s['hotspots'][0]['provenance'].update(pointer='/scene/missing')]:
            scene=copy.deepcopy(self.scenes['room-180-state-1']);change(scene)
            self.assertTrue(validate_scene(scene,self.args)[0])

    def test_missing_localizations_and_large_overlaps_warn(self):
        scene=copy.deepcopy(self.scenes['room-203-state-1'])
        errors,warnings=validate_scene(scene,self.args)
        self.assertFalse(errors)
        self.assertTrue(any(w['kind']=='missing_expected_object' and w['object_id']=='BOTTLE' for w in warnings))
        for spot in scene['hotspots'][:2]:spot['rect']=[0,0,240,150]
        self.assertTrue(any(w['kind']=='large_overlap' for w in validate_scene(scene,self.args)[1]))

    def test_changed_image_requires_new_vision_observation(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);args=arguments(['--pilot',str(root),'--output',str(root/'out')])
            pilot=read(self.args.pilot/'manifest.json')
            from generate import write
            pilot['records'][0]['rgb_preview']='previews/changed.png'
            write(root/'manifest.json',pilot)
            path=root/pilot['records'][0]['rgb_preview'];path.parent.mkdir(parents=True)
            path.write_bytes(b'changed image')
            with self.assertRaisesRegex(ValueError,'Image changed'):generate(args)
            self.assertFalse(args.output.exists())


if __name__=='__main__':unittest.main()
