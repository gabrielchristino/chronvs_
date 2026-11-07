"""Recoverable serial access for guided collectors; disk errors stay visible."""
import serial


class CaptureDisconnected(RuntimeError):
    pass


DISCONNECT_MESSAGE = (
    "Conexão USB interrompida. As amostras aceitas estão preservadas; "
    "esta captura não foi salva. Reconecte o relógio, acorde a tela, "
    "selecione ON, desligue ECO e abra Vox. "
    "Pressione Enter para preparar e repetir a mesma amostra; q para sair."
)


class SerialCaptureLink:
    def __init__(self, name, baud=115200):
        self.port = serial.Serial(port=None, baudrate=baud, timeout=.5)
        self.port.dtr = False
        self.port.rts = False
        self.port.port = name

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        try:
            self.port.close()
        except OSError:
            pass  # Windows may also fail to close an already removed device.

    def prepare(self):
        try:
            if not self.port.is_open:
                self.port.open()
            self.port.reset_input_buffer()
        except OSError as error:
            self.close()
            raise CaptureDisconnected(str(error)) from error

    def readline(self, size=1024):
        try:
            return self.port.readline(size)
        except OSError as error:
            self.close()
            raise CaptureDisconnected(str(error)) from error
