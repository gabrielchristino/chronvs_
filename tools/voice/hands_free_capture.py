"""Watch-driven collection: the next BEGIN confirms the previous complete WAV.

Until then, r can repeat it without changing accepted records. A complete
pending.wav survives interruption and is replayed when resuming.
"""
import time

from dataset import PCMReceiver, pcm_stats, read_wav, write_wav
from serial_capture import CaptureDisconnected


class ConsoleKeys:
    def __init__(self):
        import msvcrt
        self.console = msvcrt

    def poll(self):
        if not self.console.kbhit():
            return None
        key = self.console.getwch()
        if key in ("\x00", "\xe0"):
            self.console.getwch()
            return None
        if key == "\x03":
            raise KeyboardInterrupt
        return key.lower()


def collect_hands_free(port, transport, directory, prompts, start, save,
                       playback=None, keys=None, clock=time.monotonic, sleep=time.sleep):
    """save(index, pcm, stats) commits pending.wav; q accepts staged audio.

    No stdin line input, no automatic voice recording and no firmware commands.
    Only explicit recorder taps produce audio. Last sample auto-confirms after
    five seconds following playback; r remains available during that interval.
    """
    keys = keys or ConsoleKeys()
    pending = directory / "pending.wav"
    index, staged = start, None
    receiver = PCMReceiver()
    connected, receiving, repeat_capture = False, False, False
    retry_at, receive_deadline, finish_deadline = 0, None, None

    def show():
        if staged is None:
            print(f"\n[{index + 1}/{len(prompts)}] {prompts[index]}")
            print("Toque no gravador do relógio. r repete; q pausa. Não precisa de Enter.", flush=True)
        elif index + 1 < len(prompts):
            print(f"\n[{index + 1}/{len(prompts)}] Áudio pronto. r repete este áudio; q salva e pausa.")
            print(f"Próxima [{index + 2}/{len(prompts)}]: {prompts[index + 1]}")
            print("Toque no gravador para confirmar o áudio anterior e gravar a próxima.", flush=True)
        else:
            print("\nÚltimo áudio pronto: r repete; confirmação automática em 5 segundos.", flush=True)

    def stage(pcm, persist=True):
        nonlocal staged, finish_deadline
        stats = pcm_stats(pcm)
        if persist:
            write_wav(pending, pcm)
        staged = (pcm, stats)
        print(f"RMS={stats['rms']:.4f}; saturação={stats['clipped_fraction']:.2%}", flush=True)
        if playback:
            playback(pending)
        finish_deadline = clock() + 5 if index + 1 == len(prompts) else None
        show()

    if start >= len(prompts):
        print("Sessão já concluída.")
        return
    if pending.exists():
        print(f"Recuperando áudio pendente [{index + 1}/{len(prompts)}]: {prompts[index]}")
        stage(read_wav(pending), persist=False)
    else:
        show()

    while True:
        key = keys.poll()
        if key == "q":
            if staged is not None:
                save(index, *staged)
            print("Sessão concluída." if staged is not None and index + 1 == len(prompts)
                  else "Coleta pausada. Repita o mesmo comando para continuar.")
            return
        if key == "r":
            if staged is not None:
                pending.unlink()  # Only the unaccepted sample, by explicit r.
                staged, finish_deadline = None, None
                receiver = PCMReceiver()
                show()
            elif receiving:
                repeat_capture = True
                print("Esta tentativa será descartada; repita após terminar o envio.", flush=True)
            else:
                show()
        if finish_deadline is not None and clock() >= finish_deadline:
            save(index, *staged)
            print("Sessão concluída.")
            return
        if staged is not None and index + 1 == len(prompts):
            sleep(.05)
            continue
        if not connected:
            if clock() < retry_at:
                sleep(.05)
                continue
            try:
                port.prepare()
                connected = True
                print("Relógio conectado. Aguardando o toque no gravador.", flush=True)
            except CaptureDisconnected:
                if retry_at == 0:
                    print("USB indisponível. Reconecte o relógio; vou tentar novamente. q pausa.", flush=True)
                retry_at = clock() + 1
                continue
        try:
            line = port.readline(1024)
        except CaptureDisconnected:
            connected, receiving, repeat_capture = False, False, False
            receiver, receive_deadline = PCMReceiver(), None
            retry_at = clock() + 1
            print("USB desconectou. Áudios completos preservados; tentativa parcial descartada.", flush=True)
            print("Reconecte, acorde o relógio, selecione ON, desligue ECO e abra Vox.", flush=True)
            continue
        if line:
            transport.write(line)
            transport.flush()
            try:
                text = line.decode("ascii", errors="replace")
                pcm = receiver.feed(text)
                if text.strip().startswith("VOXPCM BEGIN") and receiver.data is not None:
                    if staged is not None:
                        if index + 1 == len(prompts):
                            continue
                        save(index, *staged)
                        index += 1
                        staged = None
                    receiving, repeat_capture = True, False
                    receive_deadline = clock() + 90
                    print(f"Recebendo [{index + 1}/{len(prompts)}]...", flush=True)
                if pcm is not None:
                    receiving, receive_deadline = False, None
                    if repeat_capture:
                        repeat_capture = False
                        show()
                    else:
                        stage(pcm)
            except ValueError as error:
                print(f"Amostra recusada: {error}", flush=True)
                receiver, receiving, receive_deadline = PCMReceiver(), False, None
                repeat_capture = False
                show()
        if receiving and clock() >= receive_deadline:
            receiver, receiving, receive_deadline = PCMReceiver(), False, None
            repeat_capture = False
            print("Envio incompleto: repita a mesma amostra.", flush=True)
            show()
