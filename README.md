# Tema 2 APD - Alexandra Bulgaru 331CD

## Introducere
Tema 2 simuleaza functionalitatile de baza ale protocolului BitTorrent, folosind MPI si PThreads pentru a gestiona comunicatia si operatiile dintre mai multi clienti si un tracker centralizat.

**Tracker**:
- primeste de la fiecare peer informatii precum numele fisierului, numarul de segmente si hash-urile fiecarui segment;
- mentine o lista de peers care detin segmente ale unui anumit fisier;
- cand un peer solicita informatii, raspunde cu detaliile necesare pentru a facilita descarcarea;
- lucreaza continuu, asteptand si procesand cererile de la peers pana cand toti clientii sunt pregatiti sa finalizeze descarcarile.

**Peers**:
- citesc fisierele de intrare pentru a initializa lista de fisiere detinute si fisiere dorite pentru download;
- trimit informatii despre numele fisierului, numarul de segmente si hash-urile fiecarui segment catre tracker si primesc confirmari ca fisierele au fost inregistrate corect;
- thread-ul de download descarca segmentele de la alti peers care le detin, dupa ce a primit informatii despre proprietarii fisierelor de la tracker;
- thread-ul de upload asculta pentru cereri de la alti peers care doresc sa descarce segmente din fisierele detinute; raspunde la cereri trimitand fisierele solicitate.

**Structuri**:
- _file_t_: reprezinta datele unui fisier
- _client_t_: stocheaza informatiile despre un peer
- _swarm_t_: un swarm al unui anumit fisier
- _peer_info_t_: stocheaza informatiile despre un peer specific

## Flux
- Tracker-ul asteapta sa primeasca de la fiecare peer numarul de fisiere detinute, primind pentru fiecare numele, numarul de segmente si hash-urile segmentelor.
- Tracker-ul asculta apoi dupa cereri de la peers. Cand primeste un mesaj de tip GET_OWNERS, raspunde cu numarul de segmente ale fisierului si lista de peers care detin acel fisier.
- Tracker-ul monitorizeaza starea de pregatire a tuturor peers si trimite un mesaj de DONE in momentul in care toti sunt gata sa isi finalizeze descarcarile.
- Fiecare peer citeste fisierul de intrare pentru a-si popula lista de fisiere detinute si apoi le trimite catre tracker. 
- Tread-ul de download itereaza prin fisierele dorite. Pentru fiecare fisier, peer trimite o cerere GET_OWNERS catre tracker pentru a obtine lista de peers care detin fisierul. Peers solicita segmentele necesare de la alti peers si le salveaza local. Dupa descarcarea completa, salveaza fisierul descarcat si notifica tracker-ul ca a finalizat descarcarile.
- Thread-ul de upload asculta pentru cereri de la alti peers. Cand primeste o cerere GET_SEGMENT, peer verifica daca detine segmentul solicitat si il trimite inapoi.
- Dupa finalizarea descarcarilor, peer trimite un mesaj READY si asteapta mesajul DONE pentru a se incheia.
