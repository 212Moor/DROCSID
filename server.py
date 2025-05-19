import asyncio
import re
from collections import defaultdict
from datetime import datetime

class DROCSIDServer:
    def __init__(self):
        self.users = {}  
        self.groups = defaultdict(dict)  
        self.active_writers = set()
        print("Serveur DROCSID initialisé. En attente de connexions...")

    async def handle_client(self, reader, writer):
        addr = writer.get_extra_info('peername')
        print(f"Nouvelle connexion depuis {addr}")
        self.active_writers.add(writer)
        writer.write(b"TCHAT 1\n")

        try:
            while True:
                data = await reader.readuntil(b'\n')
                cmd = data.decode().strip()
                if not cmd:
                    continue

                print(f"Commande reçue: {cmd}")  

                if cmd.startswith("LOGIN"):
                    pseudo = cmd.split()[1]
                    if not re.match(r'^[a-zA-Z0-9_-]{1,16}$', pseudo):
                        writer.write(b"ERROR 20\n")
                        print(f"Pseudo invalide: {pseudo}")
                    elif pseudo in self.users.values():
                        writer.write(b"ERROR 23\n")
                        print(f"Pseudo déjà pris: {pseudo}")
                    else:
                        self.users[writer] = pseudo
                        writer.write(b"OKAY!\n")
                        print(f"Utilisateur connecté: {pseudo}")

                elif cmd.startswith("CREAT"):
                    group = cmd.split()[1]
                    if group in self.groups:
                        writer.write(b"ERROR 33\n")
                        print(f"Groupe existe déjà: {group}")
                    else:
                        self.groups[group] = {}
                        writer.write(b"OKAY!\n")
                        print(f"Groupe créé: {group}")


        except (asyncio.IncompleteReadError, ConnectionResetError) as e:
            print(f"Client déconnecté: {addr} ({e})")
        finally:
            self.cleanup_client(writer)
            print(f"Connexion fermée: {addr}")

    def cleanup_client(self, writer):
        pseudo = self.users.pop(writer, None)
        if pseudo:
            for group in self.groups.values():
                group.pop(pseudo, None)
            print(f"Utilisateur déconnecté: {pseudo}")
        writer.close()
        self.active_writers.remove(writer)

async def main():
    server = DROCSIDServer()
    srv = await asyncio.start_server(server.handle_client, '127.0.0.1', 8888)
    print("Serveur DROCSID démarré sur le port 8888")
    async with srv:
        await srv.serve_forever()

asyncio.run(main())