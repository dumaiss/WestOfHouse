"""Test-only driver for the unchanged libzvm C core through its public API."""
import ctypes as C
from pathlib import Path


class Context(C.Union):
    _fields_ = [('alignment', C.c_void_p), ('storage', C.c_uint8 * 1536)]


class VM:
    def __init__(self, library, story):
        self.lib = C.CDLL(str(Path(library).resolve()))
        self.context = Context()
        self.story = bytearray(Path(story).read_bytes())
        self.original_header = bytes(self.story[:64])
        self.buffer = (C.c_uint8 * len(self.story)).from_buffer(self.story)
        self.output = bytearray()
        self.status_events = []
        self.output_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint8, C.c_uint8)
        self.callback = self.output_type(self._output)
        self.status_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint16, C.c_uint16, C.c_uint16, C.c_uint8)
        self.status_callback = self.status_type(self._status)
        self.lib.zvm_init.argtypes = [C.POINTER(Context), C.POINTER(C.c_uint8), C.c_uint32]
        self.lib.zvm_run.argtypes = [C.POINTER(Context), C.c_uint16]
        self.lib.zvm_set_output_callback.argtypes = [C.POINTER(Context), self.output_type, C.c_void_p]
        self.lib.zvm_set_status_callback.argtypes = [C.POINTER(Context), self.status_type, C.c_void_p]
        self.lib.zvm_submit_input.argtypes = [C.POINTER(Context), C.c_char_p]
        self.lib.zvm_global.argtypes = [C.POINTER(Context), C.c_uint8, C.POINTER(C.c_uint16)]
        self.lib.zvm_profile_get.argtypes = [C.POINTER(Context)]
        self.lib.zvm_profile_get.restype = C.POINTER(C.c_uint32)
        self.lib.zvm_write_byte.argtypes = [C.POINTER(Context), C.c_uint32, C.c_uint8]
        self.lib.zvm_program_counter.restype = C.c_uint32
        self.lib.zvm_fault_address.restype = C.c_uint32
        if self.lib.zvm_init(C.byref(self.context), self.buffer, len(self.story)):
            raise ValueError('VM story initialization failed')
        self.lib.zvm_set_output_callback(C.byref(self.context), self.callback, None)
        self.lib.zvm_set_status_callback(C.byref(self.context), self.status_callback, None)
        self.last_status = None

    def _output(self, user, channel, byte):
        self.output.append(byte)
        return 0

    def _status(self, user, room, value1, value2, mode):
        self.status_events.append((room, value1, value2, mode))
        return 0

    def advance(self, command=None):
        self.output.clear()
        self.status_events.clear()
        if command is not None:
            error = self.lib.zvm_submit_input(C.byref(self.context), command.encode('ascii'))
            if error:
                raise RuntimeError('Input submission failed: ' + str(error))
        for _ in range(10000):
            status = self.lib.zvm_run(C.byref(self.context), 1200)
            if status == 4:  # ZVM_YIELDED
                continue
            self.last_status = status
            if status not in (1, 5):  # NEED_INPUT or FINISHED
                error = self.lib.zvm_error(C.byref(self.context))
                pc = self.lib.zvm_fault_address(C.byref(self.context))
                raise RuntimeError(f'VM status={status} error={error} instruction={pc}')
            return bytes(self.output)
        raise RuntimeError('Instruction budget exceeded')

    def global_value(self, index):
        value = C.c_uint16()
        if self.lib.zvm_global(C.byref(self.context), index, C.byref(value)):
            raise RuntimeError('Global read failed')
        return value.value

    def write_byte(self, address, value):
        if self.lib.zvm_write_byte(C.byref(self.context), address, value):
            raise RuntimeError('Logical write failed')

    def write_word(self, address, value):
        self.write_byte(address, (value >> 8) & 255)
        self.write_byte(address + 1, value & 255)

    def set_global(self, index, value):
        base = int.from_bytes(self.story[12:14], 'big')
        self.write_word(base + index * 2, value)

    @property
    def instructions(self):
        return self.lib.zvm_profile_get(C.byref(self.context))[0]

    def object_entry(self, object_id):
        base = int.from_bytes(self.story[10:12], 'big')
        return base + 62 + (object_id - 1) * 9

    def move_object(self, object_id, destination):
        """Fixture setup only: preserve V3 parent/sibling/child relationships."""
        entry = self.object_entry(object_id)
        parent, sibling = self.story[entry + 4:entry + 6]
        if parent:
            link = self.object_entry(parent) + 6
            for _ in range(255):
                child = self.story[link]
                if child == object_id:
                    self.write_byte(link, sibling)
                    break
                if not child:
                    raise RuntimeError('Broken fixture object chain')
                link = self.object_entry(child) + 5
            else:
                raise RuntimeError('Cyclic fixture object chain')
        new_head = self.object_entry(destination) + 6
        self.write_byte(entry + 4, destination)
        self.write_byte(entry + 5, self.story[new_head])
        self.write_byte(new_head, object_id)

    def attribute(self, object_id, flag, enabled):
        address = self.object_entry(object_id) + flag // 8
        mask = 0x80 >> (flag & 7)
        value = self.story[address] | mask if enabled else self.story[address] & ~mask
        self.write_byte(address, value)
