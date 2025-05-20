import asyncio
import re
from collections import defaultdict
from datetime import datetime
import time

class DROCSIDServer:
    def __init__(self):
        self.users = {}  # {writer: (pseudo, last_active)}
        self.groups = defaultdict(dict)  # {group: {pseudo: writer}}
        self.active_writers = set()
        print("Serveur DROCSID initialisé. En attente de connexions...")

    async def handle_client(self, reader, writer):
        addr = writer.get_extra_info('peername')
        print(f"Nouvelle connexion depuis {addr}")
        self.active_writers.add(writer)
        writer.write(b"srv: TCHAT 1\n")

        try:
            while True:
                data = await asyncio.wait_for(reader.readuntil(b'\n'), timeout=5000)
                cmd = data.decode().strip()
                if not cmd:
                    continue

                if writer in self.users:
                    self.users[writer] = (self.users[writer][0], time.time())

                print(f"Commande reçue: {cmd}")

                if cmd.startswith("LOGIN"):
                    await self.handle_login(writer, cmd)
                elif cmd.startswith("CREAT"):
                    await self.handle_creat(writer, cmd)
                elif cmd.startswith("ENTER"):
                    await self.handle_enter(writer, cmd)
                elif cmd.startswith("SPEAK"):
                    await self.handle_speak(reader, writer, cmd)
                elif cmd.startswith("LEAVE"):
                    await self.handle_leave(writer, cmd)
                elif cmd.startswith("LSMEM"):
                    await self.handle_lsmem(writer, cmd)
                elif cmd.startswith("MSGPV"):
                    await self.handle_msgpv(reader, writer, cmd)
                elif cmd == "ALIVE":
                    writer.write(b"srv: ALIVE\n")
                else:
                    writer.write(b"srv: ERROR 11\n")

        except (asyncio.IncompleteReadError, ConnectionResetError, asyncio.TimeoutError) as e:
            print(f"Client déconnecté: {addr} ({e})")
        finally:
            await self.cleanup_client(writer)
            print(f"Connexion fermée: {addr}")

    async def handle_login(self, writer, cmd):
        try:
            pseudo = cmd.split()[1]
            if not re.match(r'^[a-zA-Z0-9_-]{1,16}$', pseudo):
                writer.write(b"srv: ERROR 20\n")
                print(f"Pseudo invalide: {pseudo}")
            elif pseudo in [p for p, _ in self.users.values()]:
                writer.write(b"srv: ERROR 23\n")
                print(f"Pseudo déjà pris: {pseudo}")
            else:
                self.users[writer] = (pseudo, time.time())
                writer.write(b"srv: OKAY!\n")
                print(f"Utilisateur connecté: {pseudo}")
        except IndexError:
            writer.write(b"srv: ERROR 10\n")

    async def handle_creat(self, writer, cmd):
        try:
            group = cmd.split()[1]
            if not re.match(r'^[a-zA-Z0-9_-]{1,16}$', group):
                writer.write(b"srv: ERROR 30\n")
            elif group in self.groups:
                writer.write(b"srv: ERROR 33\n")
            else:
                self.groups[group] = {}
                writer.write(b"srv: OKAY!\n")
                print(f"Groupe créé: {group}")
        except IndexError:
            writer.write(b"srv: ERROR 10\n")

    async def handle_enter(self, writer, cmd):
        try:
            group = cmd.split()[1]
            if group not in self.groups:
                writer.write(b"srv: ERROR 31\n")
            elif writer not in self.users:
                writer.write(b"srv: ERROR 01\n")
            else:
                pseudo = self.users[writer][0]
                if pseudo in self.groups[group]:
                    writer.write(b"srv: ERROR 35\n")
                else:
                    self.groups[group][pseudo] = writer
                    writer.write(b"srv: OKAY!\n")
                    ts = int(datetime.now().timestamp())
                    msg = f"srv: ENTER {group} {pseudo} {ts}\n"
                    await self.broadcast(group, msg, exclude=writer)
                    print(f"{pseudo} a rejoint {group}")
        except IndexError:
            writer.write(b"srv: ERROR 10\n")

    async def handle_speak(self, reader, writer, cmd):
        try:
            group = cmd.split()[1]
            if group not in self.groups:
                writer.write(b"srv: ERROR 31\n")
                return
            
            pseudo = self.users[writer][0]
            if pseudo not in self.groups[group]:
                writer.write(b"srv: ERROR 34\n")
                return

            message = await self.read_multiline(reader)
            if not message.strip():
                writer.write(b"srv: ERROR 10\n")
                return

            ts = int(datetime.now().timestamp())
            broadcast_msg = f"srv: SPEAK {pseudo} {group} {ts}\n{message}\n.\n"
            await self.broadcast(group, broadcast_msg)
            print(f"Message diffusé dans {group} par {pseudo}")

        except IndexError:
            writer.write(b"srv: ERROR 10\n")

    async def handle_leave(self, writer, cmd):
        try:
            group = cmd.split()[1]
            if group not in self.groups:
                writer.write(b"srv: ERROR 31\n")
                return
                
            pseudo = self.users[writer][0]
            if pseudo not in self.groups[group]:
                writer.write(b"srv: ERROR 34\n")
                return
                
            del self.groups[group][pseudo]
            writer.write(b"srv: OKAY!\n")
            
            ts = int(datetime.now().timestamp())
            notif = f"srv: LEAVE {group} {pseudo} {ts}\n"
            await self.broadcast(group, notif)
            print(f"{pseudo} a quitté {group}")
            
        except IndexError:
            writer.write(b"srv: ERROR 10\n")

    async def handle_lsmem(self, writer, cmd):
        try:
            group = cmd.split()[1]
            if group not in self.groups:
                writer.write(b"srv: ERROR 31\n")
                return
                
            pseudo = self.users[writer][0]
            if pseudo not in self.groups[group]:
                writer.write(b"srv: ERROR 34\n")
                return
                
            members = list(self.groups[group].keys())
            writer.write(f"srv: LSMEM {group} {len(members)}\n".encode())
            for member in members:
                writer.write(f"{member}\n".encode())
            print(f"Liste membres demandée pour {group} par {pseudo}")
            
        except IndexError:
            writer.write(b"srv: ERROR 10\n")

    async def handle_msgpv(self, reader, writer, cmd):
        try:
            parts = cmd.split()
            if len(parts) < 2:
                writer.write(b"srv: ERROR 10\n")
                return
                
            dest_pseudo = parts[1]
            sender_pseudo = self.users[writer][0]
            
            # Trouver le writer du destinataire
            target_writer = None
            for w, (p, _) in self.users.items():
                if p == dest_pseudo:
                    target_writer = w
                    break
                    
            if not target_writer:
                writer.write(b"srv: ERROR 21\n")
                return
                
            # Lire le message multiligne
            message = await self.read_multiline(reader)
            if not message.strip():
                writer.write(b"srv: ERROR 10\n")
                return
                
            # Envoyer le message
            ts = int(datetime.now().timestamp())
            header = f"srv: MSGPV {sender_pseudo} {ts}\n"
            full_msg = header + message + "\n.\n"
            
            target_writer.write(full_msg.encode())
            await target_writer.drain()
            writer.write(b"srv: OKAY!\n")
            print(f"Message privé de {sender_pseudo} à {dest_pseudo}")
            
        except IndexError:
            writer.write(b"srv: ERROR 10\n")
        except Exception as e:
            print(f"Erreur MSGPV: {e}")
            writer.write(b"srv: ERROR 00\n")

    async def read_multiline(self, reader):
        lines = []
        while True:
            line = (await reader.readuntil(b'\n')).decode()
            if line.strip() == '.':
                break
            lines.append(line)
        return ''.join(lines)

    async def broadcast(self, group, message, exclude=None):
        if group not in self.groups:
            return
            
        for pseudo, writer in self.groups[group].items():
            if writer != exclude:
                try:
                    writer.write(message.encode())
                    await writer.drain()
                except:
                    await self.cleanup_client(writer)

    async def cleanup_client(self, writer):
        if writer in self.users:
            pseudo = self.users[writer][0]
            for group in list(self.groups.keys()):
                if pseudo in self.groups[group]:
                    del self.groups[group][pseudo]
                    ts = int(datetime.now().timestamp())
                    msg = f"srv: LEAVE {group} {pseudo} {ts}\n"
                    await self.broadcast(group, msg)
            del self.users[writer]
            print(f"Utilisateur déconnecté: {pseudo}")
        writer.close()
        self.active_writers.discard(writer)

async def check_inactive(server):
    while True:
        await asyncio.sleep(5000)
        current_time = time.time()
        for writer in list(server.users.keys()):
            _, last_active = server.users[writer]
            if current_time - last_active > 5000:
                writer.write(b"srv: ALIVE\n")
                await writer.drain()
            if current_time - last_active > 10000:
                await server.cleanup_client(writer)

async def main():
    server = DROCSIDServer()
    asyncio.create_task(check_inactive(server))
    srv = await asyncio.start_server(
        server.handle_client, 
        '127.0.0.1', 
        8888
    )
    print("Serveur DROCSID démarré sur le port 8888")
    async with srv:
        await srv.serve_forever()

if __name__ == "__main__":
    asyncio.run(main())
