"""An implementation of a struct parser which is fast and convenient."""

import struct

format_string_map = dict(
    uint64_t="Q",
    int64_t="q",
    uint32_t="I",
    uint16_t="H",
    int32_t="i",
    int16_t="h",
)

class BaseParser(object):
    _format_string = ""
    _fields = []
    _name = "Unknown"
    _defaults = []
    __slots__ = ("_data", "_fields", "_name", "_format_string", "_defaults")

    def __init__(self, data=None, **kwargs):
        if data is None:
            self._data = self._defaults[:]
        else:
            self._data = list(
                struct.unpack_from(self._format_string, data))

        if kwargs:
            for k, v in kwargs.iteritems():
                setattr(self, k, v)

    def __str__(self):
        result = ["Struct %s" % self._name]
        for field, data in zip(self._fields, self._data):
            result.append("  %s: %s" % (field, data))

        return "\n".join(result)

    def Pack(self):
        return struct.pack(self._format_string, *self._data)

    @classmethod
    def sizeof(cls):
        return struct.calcsize(cls._format_string)


def CreateStruct(struct_name, definition):
    fields = []
    format_string = ["<"]
    defaults = []

    for line in definition.splitlines():
        line = line.strip(" ;")
        components = line.split()
        if len(components) >= 2:
            type_format_char = format_string_map.get(components[0])
            name = components[1]

            if type_format_char is None:
                raise RuntimeError("Invalid definition %r" % line)

            try:
                if components[2] != "=":
                    raise RuntimeError("Invalid definition %r" % line)
                defaults.append(int(components[3], 0))
            except IndexError:
                defaults.append(0)

            format_string.append(type_format_char)
            fields.append(name)

    properties = dict(
        _format_string="".join(format_string),
        _fields=fields,
        _defaults=defaults,
        _name=struct_name)

    # Make accessors for all fields.
    for i, field in enumerate(fields):
        def setx(self, value, i=i):
            self._data[i] = value

        def getx(self, i=i):
            return self._data[i]

        properties[field] = property(getx, setx)

    return type(struct_name, (BaseParser,), properties)
