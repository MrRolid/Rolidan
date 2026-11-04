# Linuxový editor obrázků „schránka → úprava → soubor“ — implementační zadání

Stav: rozhodnutí J1–J8 odsouhlasena 2026-11-03 · Technologie: C++ a Qt 6

Dokument vychází z funkční analýzy inspirované IrfanView / IrfanPaint. Analýzu nepřepisuje, ale:

1. opravuje rozpory mezi jejími částmi (oddíl A),
2. doplňuje chování, které v analýze chybělo nebo bylo nejednoznačné (oddíly B–H),
3. rozepisuje etapy do ověřitelných akceptačních kritérií (oddíl I).

Produktová rozhodnutí jsou shrnutá v oddílu J (odkazy „J1“ atd. v textu).

Pracovní název aplikace v tomto dokumentu: **Rolidan** (podle adresáře projektu).

---

## A. Rozpory v původní analýze a jejich řešení

| # | Rozpor | Řešení |
|---|---|---|
| A1 | Etapa 1 nemá vrstvu objektů, ale druhé Ctrl+V nad otevřeným obrázkem má vložit *přesouvatelný objekt*. Chování Ctrl+V v Etapě 1 tak není definované. | Etapa 1 zahrnuje základ modelu dokumentu (podklad + seznam objektů) s jediným typem objektu: **vložený obrázek**. Model se tím ověří hned, ne až s textem. |
| A2 | Oddíl 10 říká, že chování ořezu a rotace vůči anotacím je potřeba vyřešit v návrhu. Etapa 3 ale „dotažené chování ořezu a transformací u anotací“ odkládá. | Každý typ objektu se dodává včetně chování při ořezu, rotaci, zrcadlení a změně rozlišení (tabulka C4). Je to akceptační kritérium té etapy, ve které objekt vzniká. |
| A3 | Podle priorit v oddílu 8 jsou guma, kapátko a neprůhledné zakrytí „Základ“, ale nejsou v žádné etapě. Mozaika a rozmazání oblasti také ne. „Rozmazání oblasti“ (oddíl 8) a „Rozmazávací štětec“ (oddíl 7, Etapa 3) jsou dva různé nástroje. | Kapátko, neprůhledné zakrytí a mazání objektů → Etapa 2. Pixelová guma, rozmazání oblasti a mozaika → Etapa 3, protože sdílejí štětcový a pixelový engine s razítkem. Rozmazávací štětec zůstává v Etapě 3. |
| A4 | Požadavek „přepsání existujícího souboru vyžaduje potvrzení“ je v rozporu s rychlým Ctrl+S do známého souboru. | Potvrzení se ukáže jen u „Uložit jako“ na existující soubor a u **prvního** Ctrl+S, které by přepsalo soubor, ze kterého byl dokument otevřen (viz F2). |
| A5 | Nástroj „Výběr“ v panelu má dva významy: vybírání objektů a výběr oblasti pixelů. | Dva nástroje: **Ukazatel** (objekty) a **Výběr oblasti** (obdélník v obraze). **Výřez** je samostatný nástroj se ztmavením okolí. Obdélník z Výběru oblasti lze příkazem „Oříznout na výběr“ převzít do výřezu. |
| A6 | Štětec není v oddílu 10 zařazen do žádné vrstvy. Guma „mazat pixely“ naznačuje rastrovou kresbu. | Štětec vytváří **objekt** (tah), stejně jako ostatní anotace. Pixelová guma pracuje jen s podkladem. Důsledek: tah štětce nejde částečně vygumovat. Lze ho smazat celý, nebo sloučit do podkladu a gumovat pixely. |
| A7 | Ctrl+C „podle kontextu“ není definované. | Přesné chování je v tabulce B3. |

---

## B. Vstup: schránka, soubory, spuštění

### B1. Ctrl+V — co se vloží (priorita MIME typů)

Aplikace čte nabídnuté formáty v tomto pořadí a použije první vhodný:

| Pořadí | Nabízený obsah | Akce |
|---|---|---|
| 1 | `image/png` | obrázek (zachová průhlednost) |
| 2 | jiný `image/*`, který umí Qt (`image/webp`, `image/jpeg`, `image/bmp`, …) | obrázek |
| 3 | `text/uri-list` s **jedním** lokálním souborem obrázku | načte soubor jako obrázek |
| 4 | `text/uri-list` s více soubory | načte první, oznámí „Vložen 1 z N souborů“ |
| 5 | `text/uri-list` / text s adresou `http(s)://` | **nestahuje**; oznámí „Schránka obsahuje odkaz, ne obrázek. V prohlížeči použijte Kopírovat obrázek.“ |
| 6 | cokoli jiného | oznámí „Ve schránce není obrázek“, nic nemění |

Výjimka: při editaci textového objektu vkládá Ctrl+V vždy text.

### B2. Kam se vloží

| Stav | Ctrl+V | Ctrl+Shift+V („Nový ze schránky“) |
|---|---|---|
| Prázdné okno | nový dokument v rozlišení obrázku | totéž |
| Otevřený dokument | objekt „vložený obrázek“ 1:1, vystředěný ve viditelné části. Opakované vložení se posouvá o 16 px. Objekt je hned vybraný a nástroj se přepne na Ukazatel. | nový dokument v **novém okně** (J1); stávající dokument zůstane beze změny |
| Probíhá tah / rozpracovaný výřez | rozpracovaná operace se zruší (jako Esc), pak normální vložení | — |

Pokud je vložený objekt větší než plátno, oznámení nabídne tlačítko **„Rozšířit plátno“**. Plátno se zvětší tak, aby objekt pokrylo; nové místo je průhledné a jde o jeden krok Zpět. Nic se neděje automaticky.

Rozměry: vždy se použijí **pixelové rozměry** obrázku. `devicePixelRatio` z HiDPI zdrojů se ignoruje (nastaví se na 1), jinak by se screenshot z obrazovky se 150% škálováním vložil zmenšený.

### B3. Kopírování a vyjímání — co se zkopíruje

Pravidlo (J5): **bez Shiftu = jen podklad, se Shiftem = vše** (podklad + anotace). Platí stejně pro kopírování i vyjímání.

| Stav | Ctrl+C | Ctrl+Shift+C | Ctrl+X | Ctrl+Shift+X |
|---|---|---|---|---|
| Editace textu | označený text | jako Ctrl+C | označený text | jako Ctrl+X |
| Aktivní výběr oblasti | **jen podklad**: pixely podkladu ve výběru bez poznámek | **vše**: kompozit ve výběru | **jen podklad**: zkopíruje jako Ctrl+C a vymaže podklad ve výběru do průhlednosti; anotace zůstanou na místě | **vše**: zkopíruje kompozit ve výběru, vymaže podklad a smaže objekty ležící **celé** uvnitř výběru. Objekty zasahující do výběru jen částečně zůstanou. |
| Vybrané objekty | objekty jako sloučený obrázek s průhledností (interně zároveň jako objekty pro vložení zpět do Rolidanu) | jako Ctrl+C | totéž a objekty smaže | jako Ctrl+X |
| Nic není vybráno | **celý výsledný obrázek** (kompozit) — viz poznámka | celý výsledný obrázek | — | — |

Poznámka k řádku „Nic není vybráno“: Ctrl+C tu záměrně kopíruje i anotace. Nejčastější koncový krok workflow je „vložit → anotovat → Ctrl+C → vložit jinam“. Kdyby Ctrl+C bez výběru vynechalo anotace, uživatel by tiše přišel o všechny šipky a texty. Kdo chce celý podklad bez anotací, přepne na Výběr oblasti (M), stiskne Ctrl+A (výběr přes celé plátno) a Ctrl+C.

**Ctrl+Alt+C** („Kopírovat výsledek“, také tlačítko v panelu nástrojů) zkopíruje vždy celý výsledný obrázek bez ohledu na výběr. Koncový krok tak nezávisí na tom, co je zrovna vybrané.

Do schránky jde `image/png` s průhledností. Qt na X11 navíc automaticky nabízí další formáty.

### B4. Schránka na Waylandu a X11 — známá úskalí

- **Obsah schránky po zavření aplikace.** Na Waylandu (a na X11 bez správce schránky) data poskytuje běžící aplikace. Po jejím ukončení zmizí. Hlavní workflow „zkopírovat výsledek a zavřít“ by tak tiše selhal. Řešení (J4): volba v Nastavení **„Zachovat obsah schránky po zavření“** (výchozí: zapnuto). Když je zapnutá a aplikace po zavření posledního okna vlastní schránku, proces běží dál na pozadí bez okna, dokud schránku nepřevezme jiná aplikace (stejný princip jako `wl-copy`). Když je vypnutá, aplikace se ukončí hned a obsah schránky zmizí; při zavření se to jednou oznámí s odkazem na nastavení. Na X11 se správcem schránky Qt předá data přes `SAVE_TARGETS`.
- **Čtení schránky vyžaduje fokus.** Wayland zpřístupní schránku jen oknu s fokusem klávesnice. Parametr `--paste` (B6) proto musí počkat na první získání fokusu a teprve pak schránku číst.
- Testovat nativně na Waylandu (`QT_QPA_PLATFORM=wayland`, balík qt6-wayland) i přes XWayland/X11.

### B5. Přetažení souborů (drag & drop)

| Cíl | Akce |
|---|---|
| Prázdné okno | otevře soubor jako dokument |
| Otevřený dokument | vloží soubor jako objekt (stejně jako Ctrl+V) |
| Více souborů | použije první, oznámí počet ignorovaných |
| Odkaz z prohlížeče bez obrazových dat | stejné oznámení jako B1/5 |

Pokud přetažení nabízí obrazová data i URL (některé prohlížeče), mají přednost obrazová data.

### B6. Spouštění a integrace do systému

- `rolidan` — prázdné okno
- `rolidan soubor.png` — otevře soubor
- `rolidan --paste` — vloží schránku hned po získání fokusu. Hodí se na systémovou klávesovou zkratku; Wayland nedovoluje aplikacím registrovat globální zkratky, takže ji nastaví uživatel v prostředí.
- `rolidan -` — načte obrázek ze stdin (např. `grim -g "$(slurp)" - | rolidan -`)
- `.desktop` soubor s `MimeType=` pro podporované formáty. `app_id` na Waylandu odpovídá jménu `.desktop` souboru, jinak chybí ikona v panelu.

### B7. Načítání souborů

- `QImageReader::setAutoTransform(true)` — respektovat EXIF orientaci fotografií z telefonu.
- **Zvýšit `QImageReader::allocationLimit`** (výchozí limit Qt 6 je 256 MB a obrázky nad cca 8000×8000 by se bez chybové hlášky nenačetly). Navrženo 2 GB.
- Interní formát podkladu: 8 bitů na kanál, `Format_ARGB32_Premultiplied`, sRGB. Při načtení 16bitového obrázku nebo obrázku s ICC profilem se převede a stavový řádek ukáže „Převedeno na 8 bitů / sRGB“.
- Animované GIF/WebP: načte se první snímek s oznámením.
- Nepodporovaný nebo poškozený soubor: chybové oznámení, rozpracovaný dokument zůstane beze změny.

---

## C. Model dokumentu

### C1. Struktura

```text
Document
├── base: QImage                      // rastrový podklad, jediný zdroj pixelů
├── objects: vector<Annotation>       // pořadí = pořadí vykreslení (z-order)
│     každý objekt: id, typ, geometrie, styl, transformace
├── canvasSize                        // = base.size(), plátno a podklad mají vždy stejnou velikost
├── undoStack: QUndoStack
└── meta: cesta k souboru, formát, příznak neuložených změn
```

Stav, který **není** součástí dokumentu ani historie: zoom, posun pohledu, aktivní nástroj, výběr oblasti, výběr objektů, nastavení nástrojů.

### C2. Jednotky

Všechny souřadnice a rozměry jsou v **pixelech obrázku**: poloha, tloušťka čáry, velikost písma (px, ne pt), průměr štětce. Zoom ani škálování obrazovky je nemění. Text velikosti 24 px vypadá ve výsledném souboru stejně, ať byl napsán při zoomu 25 % nebo 400 %.

### C3. Co který nástroj čte a kam zapisuje

| Nástroj / operace | Čte | Zapisuje |
|---|---|---|
| Text, šipka, čára, tvary, štětec, zvýrazňovač, neprůhledné zakrytí, číslovaná značka | — | nový objekt |
| Vložený obrázek | schránka / soubor | nový objekt |
| Kapátko | **kompozit** (to, co uživatel vidí) | barva nástroje |
| Klonovací razítko | podklad ve stavu **před začátkem tahu** | podklad |
| Pixelová guma | — | podklad (do průhlednosti) |
| Rozmazání oblasti, mozaika | podklad | podklad (destruktivně, viz H1) |
| Rozmazávací štětec | podklad | podklad |
| Plechovka | podklad | podklad |
| Jas / kontrast / sytost | podklad | podklad |
| Sloučit objekt / všechny objekty | kompozit vybraných objektů | podklad; objekty se odstraní |

### C4. Transformace celého dokumentu a objekty

| Operace | Podklad | Objekty |
|---|---|---|
| Ořez | ořízne | posunou se. Objekty **celé mimo** nový rozsah se smažou (ve stejném kroku Zpět). Částečně zasahující zůstanou celé, ořízne je až vykreslení a export. |
| Rotace 90° / 180° | otočí | otočí se poloha i obsah: text se otočí spolu s obrázkem a zůstane u věci, kterou popisuje |
| Libovolná rotace (Etapa 4) | otočí s převzorkováním | jako 90° |
| Zrcadlení | zrcadlí | zrcadlí se poloha a geometrie (šipky, tvary, tahy, vložené obrázky). **Text se nezrcadlí**, zrcadlí se jen jeho poloha, aby zůstal čitelný (J2). |
| Změna rozlišení | převzorkuje | geometrie se škáluje stejně jako podklad. Písmo a tloušťky čar se škálují také. Při nestejném poměru stran se použije menší z obou koeficientů. |
| Změna plátna | posune podle kotvy, doplní barvou / průhledností | posunou se o stejný vektor |

Z toho plyne požadavek na model: **každý objekt musí mít obecnou afinní transformaci** (posun, rotace o násobky 90°, později libovolná), i když UI zatím nedovoluje objekty volně otáčet.

### C5. Vykreslení

- Kompozit = podklad, nad ním objekty v pořadí, oříznuto na plátno.
- Export i kopírování do schránky používají **stejnou** renderovací funkci jako zobrazení při 100 %. Tím je požadavek „export odpovídá tomu, co uživatel vidí“ splněn konstrukcí, ne testováním.
- Objekty mimo plátno se v editoru zobrazí poloprůhledně, aby šly najít a přesunout zpět. Do exportu se nedostanou.
- Průhlednost se v editoru zobrazuje šachovnicí.

---

## D. Nástroje — upřesnění chování

### D1. Společné pro objekty

- Když je vybraný objekt, panel nastavení ukazuje **jeho** vlastnosti. Změna se použije na objekt **a zároveň** se stane výchozí hodnotou nástroje.
- Tažení posuvníku nebo úchytu je **jeden** krok historie (`QUndoCommand::mergeWith` pro po sobě jdoucí změny stejné vlastnosti stejného objektu).
- Delete při vybraném objektu smaže objekt. Při aktivním výběru oblasti bez vybraného objektu vymaže pixely podkladu ve výběru.
- Pořadí: Ctrl+] dopředu, Ctrl+[ dozadu, Ctrl+Shift+] / [ úplně nahoru a dolů. Totéž i Ctrl+PgUp / PgDn (se Shiftem úplně), protože hranaté závorky jsou na některých rozloženích (CZ, DE) nepohodlné. Ctrl+D duplikuje s posunem o 16 px.
- Šipky na klávesnici posouvají vybraný objekt o 1 px, se Shiftem o 10 px. Série posunů je jeden krok Zpět.
- **Volné otáčení** (Etapa 6): vybraný objekt má nad sebou kulatý úchyt; tažením se otáčí kolem středu, Shift přichytává po 15°, během tažení se ukazuje úhel, jeden krok Zpět. Otáčí se obrázky, texty (i bubliny; špička bubliny zůstává na místě), obdélníky, elipsy, zakrytí, zvýraznění, tahy a lomené čáry. Šipky a čáry se natáčejí koncovými body, číslované značky se neotáčejí. Otočené obdélníky a obrázky se zvětšují ve vlastní ose (rohové úchyty a kurzory se natáčejí s objektem); otočený text se edituje rovně nad svým středem. Když není nad objektem místo, úchyt je pod ním.

### D2. Text

- **Klik** vytvoří text s automatickou šířkou. **Tažení** vytvoří textové pole s pevnou šířkou a zalamováním.
- Styl (font, velikost, barva, tučné, kurzíva, podtržení, zarovnání, pozadí, obrys) platí pro **celý objekt**.
- Formátování po znacích (Etapa 4): při editaci textu s označenou částí mění Ctrl+B / Ctrl+I / Ctrl+U a tlačítka B / I / U a barva v panelu jen označené znaky. Bez označení platí pro celý text. Stav určuje první označený znak (tučný → zruší tučné). Ukládá se jako seznam úseků (začátek, délka, přepsané vlastnosti) v projektu i ve schránce. Obrys textu je vždy jednobarevný. Výchozí hodnoty nástroje se změnou části textu nemění.
- Enter = nový řádek. **Esc a Ctrl+Enter ukončí editaci a text zachovají.** Klik mimo text ukončí editaci. Prázdný text se po ukončení editace smaže.
- Při editaci textu se vypínají jednopísmenné zkratky nástrojů a posun pohledu mezerníkem.
- Musí fungovat vstupní metody (IME, mrtvé klávesy, compose) a české znaky. Zajistí to `QInputMethodEvent`; je potřeba to výslovně otestovat.
- Dvojklik na text v nástroji Ukazatel = editace.

### D3. Šipka, čára

- Výchozí velikost hrotu je relativní: 4× tloušťka čáry. Volitelně absolutní velikost v px.
- Shift = úhel po 15°.
- Úchyty na obou koncích. Tažení za tělo šipky posune celou šipku.

### D4. Zvýrazňovač

- Celý tah se vykresluje **jako jedna cesta s jednou průhledností**, ne jako řada skládaných bodů. To konstrukčně řeší požadavek „žádné tmavé fleky v rámci tahu“.
- Režim skládání **Multiply** (`QPainter::CompositionMode_Multiply`). Tmavý text pod zvýrazněním zůstane tmavý a čitelný. Dva různé tahy přes sebe ztmavnou, stejně jako skutečný zvýrazňovač; to je v pořádku.
- Volný tah, se Shiftem rovná čára. Obdélníkové zvýraznění je samostatný režim nástroje.
- Výchozí barva žlutá, výchozí šířka 18 px.

### D5. Klonovací razítko

| Parametr | Specifikace |
|---|---|
| Nastavení zdroje | Ctrl+klik (Alt+klik v mnoha prostředích přebírá správce oken pro posun okna). Pravé tlačítko jako alternativa. |
| Velikost | 1–1000 px, `[` a `]` mění velikost |
| Tvrdost | 0–100 %. Krytí masky: 1 uvnitř poloměru `r·tvrdost`, mezi ním a `r` plynulý pokles funkcí *smoothstep*, mimo `r` nula. |
| Krytí | 0–100 %, **strop** pro celý tah |
| Rozestup otisků | 15 % průměru |
| Skládání v rámci tahu | maska tahu se akumuluje **maximem**, ne sčítáním. Překrývající se otisky tak nepřekročí nastavené krytí a nevznikají fleky. Výsledek = mix(podklad před tahem, zdroj, maska × krytí). |
| Zarovnání | výchozí zarovnané: posun zdroj–cíl se drží mezi tahy. Volba „nezarovnané“: každý tah začíná u původního zdroje. |
| Zdroj mimo obrázek | tyto pixely se nekopírují (maska 0) |
| Průhlednost | kopíruje se i alfa kanál |
| Náhled | obrys štětce v cíli, křížek ve zdroji. Při velkém zoomu se obrys kreslí v souřadnicích pohledu. |
| Historie | jeden tah = jeden krok. Ukládá se jen ohraničující obdélník změněné oblasti. |

Doplněk: přepínač pohledu **„Skrýt anotace“** (Ctrl+H). Usnadňuje retuš podkladu pod šipkami a textem; nemění dokument ani historii.

### D6. Výběr oblasti a výřez

- Hrany výběru se vždy přichytávají k celým pixelům.
- Výřez: ztmavení okolí, osm úchytů, posun, číselné zadání X/Y/Š/V, volný nebo pevný poměr (1:1, 4:3, 16:9, vlastní). Shift při tažení dočasně zamkne poměr. Enter potvrdí, Esc zruší.
- Výběr oblasti: stejné úchyty bez ztmavení. Slouží pro kopírování / vyjímání / Delete / zakrytí / rozmazání / mozaiku.

### D7. Zobrazení

- Kolečko myši s Ctrl = zoom ke kurzoru. Bez Ctrl = svislý posun, se Shiftem vodorovný. Touchpad: pinch zoom.
- Ctrl+0 = přizpůsobit oknu, Ctrl+1 = 100 %, Ctrl+plus / Ctrl+minus.
- Do 100 % se zobrazuje s vyhlazením, od 200 % bez vyhlazení (nejbližší soused), aby byly vidět jednotlivé pixely.
- „100 %“ na HiDPI obrazovce (J3): 1 pixel obrázku = 1 **fyzický** pixel obrazovky. Screenshot pak při 100 % vypadá přesně jako originál a je ostrý.
- Procházení složky (Etapa 4): PgDn / PgUp = další / předchozí obrázek ve složce aktuálního souboru, Home / End = první / poslední. Řazení přirozené (img2 před img10). Je to výjimka z J1: prohlížení nahrazuje zobrazený obrázek ve stejném okně; neuložené změny nabídne uložit. Při editaci textu se klávesy chovají normálně.

### D8. Úpravy barev a filtry (Etapa 4)

Menu **Barvy** a **Filtry**. Každá položka s parametry otevře dialog s posuvníky a číselnými poli, **živým náhledem přímo na plátně** (počítá se na pozadí, dialog nezamrzá; vypínatelný), tlačítkem Výchozí a jedním krokem zpět po OK. Naposledy použité hodnoty si dialog pamatuje. Pokud je aktivní výběr oblasti, filtr se použije jen na něj (rozmazání a doostření berou okolí výběru jako kontext, takže na hraně nevznikne šev); jinak na celý obrázek. Anotace se nemění.

| Menu | Položka | Parametry |
|---|---|---|
| Barvy | Vylepšit barvy (Ctrl+Shift+E) | automatické úrovně po kanálech (víc kontrastu, odstraní barevný nádech), živost, oříznutí % |
| Barvy | Jas a kontrast (Ctrl+Shift+B) | jas, kontrast −100…100, gamma 0,1–5 (nad 1 zesvětlí střední tóny) |
| Barvy | Odstín a sytost (Ctrl+Shift+U) | odstín ±180°, sytost, světlost |
| Barvy | Teplota barev | teplota (modrá ↔ žlutá), nádech (zelená ↔ purpurová) |
| Barvy | Úrovně (Ctrl+Shift+L) | černý bod, bílý bod, gama |
| Barvy | Odstíny šedi, Invertovat barvy | bez parametrů, hned |
| Barvy | Sépie, Černobílá (práh), Posterizace | síla / práh / počet úrovní |
| Filtry | Rozmazat | poloměr 0,5–100 px (gaussovské, 3 průchody box filtru) |
| Filtry | Doostřit | míra, poloměr detailů, práh (neostrá maska) |
| Filtry | Pixelizovat | velikost buňky (celý obrázek nebo výběr) |
| Filtry | Odstranit šum | mediánový filtr, poloměr 1–5 |
| Filtry | Přidat šum / Přidat barevný šum | míra; šum je deterministický (náhled se nemění při posunu jiného parametru) |
| Filtry | Vyčistit naskenovaný dokument | výsledek barevný / šedý / černobílý, vybělit papír (odhad pozadí a jeho podělení: zmizí stíny, nerovnoměrné světlo a barva papíru), ztmavit písmo, práh černobílé, odstranit tečky |
| Obrázek | Otočit o úhel (Ctrl+Alt+R) | úhel po desetinách stupně, plátno: zvětšit / zachovat / oříznout prázdné rohy, výplň rohů průhledná / bílá / černá; tlačítko „Automaticky narovnat dokument“; během dialogu mřížka |
| Obrázek | Automaticky narovnat dokument | najde sklon řádků textu (projekční profil) a otevře Otočit o úhel s nalezenou hodnotou |

Otočení o libovolný úhel a anotace: body (šipky, čáry, lomené čáry, tahy, značky) se otočí přesně, takže šipky dál ukazují na stejné místo. Obdélníky, elipsy a texty se posunou se svým středem, ale zůstanou vodorovné (texty čitelné, viz J2). Vložené obrázky se otočí s obrázkem.

### D9. Další nástroje (Etapa 4)

- **Plechovka (F)**: vyplní v podkladu oblast podobné barvy barvou nástroje. Tolerance 0–100 %, „Souvislá oblast“ (vypnuto = všechny pixely podobné barvy v celém obrázku), krytí. Průhledné pixely se považují za stejné bez ohledu na skrytou barvu.
- **Pixelizační štětec (J)**: jako rozmazávací štětec, ale mozaika; buňky jsou zarovnané na mřížku obrázku, takže se tahy navazují bez švů.
- **Narovnat (W)**: tažení podél čáry, která má být vodorovná nebo svislá (bere se bližší osa); při tažení ukazuje úhel; po puštění otevře Otočit o úhel s oříznutím prázdných rohů.

### D10. Dávkový převod (Soubor › Dávkový převod)

Seznam souborů (přidat soubory / celou složku, projekty `.rolidan` se převedou i s anotacemi), cílová složka (výchozí podsložka „převedené“), formát s volbami kodeku (dialog z F5 s náhledem na prvním obrázku), volitelné zmenšení do rozměru (nikdy nezvětšuje), jedna úprava (vylepšit barvy, odstíny šedi, vyčistit dokument, automaticky narovnat; s hodnotami naposledy zvolenými v dialozích), přípona názvu. Existující soubory se nikdy nepřepíší (přidá se „ (2)“). Běží na pozadí s ukazatelem průběhu a lze ho zastavit.

### D11. Prohlížeč (Etapa 5)

Cíl: jednoduchost IrfanView i pro prohlížení, ne jen pro úpravy.

- **Nedávné soubory** (Soubor › Otevřít nedávné): 10 posledních otevřených nebo uložených souborů, nejnovější nahoře, klávesy 1–0 v nabídce. Neexistující soubory se nezobrazí. Otevírání se řídí J1 (neprázdný dokument → nové okno). Položka „Vymazat seznam“.
- **Celá obrazovka** (F, Enter, F11, dvojklik na obrázek mimo anotace, tlačítko v liště): skryje nabídku, lišty, stavový řádek i panely. Okolí obrázku je téměř černé a bez okraje. Obrázek se přizpůsobí obrazovce; velké se zmenší, malé zůstanou ve skutečné velikosti, pokud není zapnuto „Zvětšovat malé obrázky na celé obrazovce“ (klávesa Z, menu Zobrazit, Nastavení; pamatuje se, platí i pro prezentaci). Šipky listují obrázky ve složce (bez automatického posunu), pokud je nepoužije nástroj (posun vybraných anotací). Kurzor myši se po 1,5 s bez pohybu skryje. Esc ji ukončí, pokud není co jiného zrušit (operace nástroje, výběr). Úpravy fungují dál.
- **Přizpůsobení oknu** obecně: dokud uživatel nezmění zvětšení ani neposune obraz, zobrazení sleduje velikost okna (změna okna, vstup do celé obrazovky a zpět).
- **Náhledy** (Ctrl+T, menu Zobrazit, tlačítko v liště): pás náhledů obrázků a projektů ze složky aktuálního souboru dole (panel jde přesunout). Náhledy se načítají na pozadí (dekodér JPEG zmenšuje rovnou), aktuální obrázek je zvýrazněný a posouvá se s listováním. Klik ukáže obrázek v tomto okně (s nabídkou uložení změn). Sleduje změny ve složce (přejmenování, přesun, koš, nové soubory). Viditelnost se pamatuje.
- **Prezentace** (F5, menu Zobrazit): obrázky ze složky aktuálního souboru po sobě na celé obrazovce. Interval a opakování od začátku v Nastavení (výchozí 4 s, opakovat). Mezerník pozastaví a znovu spustí, šipky a PgUp/PgDn listují (časovač začne znovu), Esc ukončí a vrátí původní režim okna. Při spuštění nabídne uložení neuložených změn; jakákoli úprava během prezentace ji ukončí.
- **Kolečko myši** (Nastavení): posouvá obrázek (výchozí), listuje obrázky ve složce (jako IrfanView; jeden obrázek na jeden zub, dotyková plocha se sčítá) nebo mění zvětšení. Ctrl + kolečko vždy mění zvětšení.
- **Informace o obrázku** (Ctrl+I, Alt+Enter): soubor (název, složka, velikost, datum změny, formát), obrázek (rozměry, megapixely, DPI pokud je nastavené, průhlednost podle skutečných pixelů, odstíny šedi), EXIF (fotoaparát, objektiv, datum pořízení, expozice, clona, ISO, ohnisko i přepočet na kinoformát, korekce expozice, blesk, autor, copyright, software) a poloha GPS s tlačítkem „Zobrazit na mapě“ (OpenStreetMap v prohlížeči; nic se neodesílá bez kliknutí). Upozornění, že metadata se do exportu nepřenášejí (F5), ale originál je obsahuje. Tlačítko „Kopírovat jako text“. EXIF se čte z JPEG (APP1), PNG (eXIf), WebP (EXIF) a TIFF vlastním čtečem, odolným proti poškozeným datům.
- **Operace se souborem** (Soubor): pracují se souborem, který dokument zobrazuje (uložený, jinak otevřený).
  - Přejmenovat (F2): dialog s označeným názvem bez přípony; existující cíl se nepřepíše. Dokument, titulek a nedávné soubory sledují nový název.
  - Kopírovat do složky (F8) / Přesunout do složky (F7): pamatuje si poslední cílovou složku. Existující soubor: Ponechat oba (výchozí, „ (2)“) / Nahradit / Zrušit. Po přesunu se ukáže další obrázek ze složky (jako po smazání).
  - Přesunout do koše (Ctrl+Delete): potvrzení s volbou „Už se neptat“ (v Nastavení jde vrátit); při neuložených změnách se ptá vždy. Používá systémový koš (freedesktop), nikdy nemaže natrvalo. Pak se ukáže další obrázek, u posledního předchozí; prázdná složka → prázdné okno.
  - Zobrazit ve správci souborů: otevře složku souboru.

---

### D12. Tisk (Soubor › Tisk, Ctrl+P; Etapa 6)

- Dialog s náhledem stránky: **Přizpůsobit stránce**, **Skutečná velikost** při zadaném dpi (výchozí je rozlišení uložené v obrázku, jinak 96 dpi) nebo **Měřítko** v % skutečné velikosti.
- Orientace (automaticky podle tvaru obrázku / na výšku / na šířku), okraje v mm, vycentrovat na stránce, tisknout anotace (skryté anotace Ctrl+H se netisknou). Tlačítko Papír… otevře nastavení stránky tiskárny.
- Tisk… otevře systémový dialog (tiskárna, počet kopií, tisk do PDF). Zvolená tiskárna a papír se pamatují do zavření programu, volby rozvržení trvale.
- Balíčky přibalují tiskový plugin CUPS, knihovnu libcups berou ze systému (bez ní funguje tisk do PDF).

### D13. Snímek obrazovky (tlačítko Snímek v hlavní liště, C; Etapa 7)

- V hlavní liště je tlačítko **Snímek** (fotoaparát). Klik nebo C: Rolidan se minimalizuje (na Waylandu skryje, protože program se nemůže sám vrátit z panelu úloh), pořídí snímek a vrátí se s ním **už vloženým** – jako Ctrl+V: do prázdného okna jako nový dokument, jinak jako nový objekt do obrázku.
- Šipka u tlačítka: celá obrazovka / oblast nebo okno, „Minimalizovat Rolidan během snímání“ (výchozí zapnuto). Políčko vedle tlačítka: zpoždění 0–60 s, 0 = „Hned“ (okamžitý snímek). Volby se pamatují.
- Snímá se přes desktopový portál `org.freedesktop.portal.Screenshot` (D-Bus): funguje na Waylandu i X11 v GNOME, KDE a dalších prostředích s xdg-desktop-portal. Volbu oblasti nebo okna nabízí samo prostředí; GNOME se poprvé zeptá na povolení. Soubor, který portál uloží (GNOME do složky Obrázky), Rolidan nemaže.
- Bez portálu na X11 se obrazovky sejmou přímo; při volbě „oblast“ se v novém dokumentu zapne nástroj Oříznout. Bez portálu na Waylandu se zobrazí chyba.

### D14. Změnit velikost (Obrázek › Změnit velikost, Ctrl+R; podle IrfanView, 2026-11-04)

- Původní velikost: px, megapixely a rozměr v cm při uloženém rozlišení.
- Nová velikost: šířka a výška v **px, cm nebo palcích**; zvlášť **procenta šířky a výšky** původního obrázku; „Zachovat poměr stran“ (pamatuje se).
- **Předvolby**: polovina, dvojnásobek, 640×480 … 3840×2160, přizpůsobit obrazovce, velikost obrazovky (bez poměru stran). S poměrem stran se obrázek vejde do zvolené velikosti.
- **Rozlišení (dpi)**: zapíše se do souboru a používá se pro cm/palce; jen změna dpi obrázek nepřevzorkuje (krok „Změna rozlišení“).
- **Metoda** (stejné filtry jako IrfanView): Lanczos (výchozí, nejostřejší), Mitchell (bikubický), Hermite, Triangle (bilineární), Bell, B-spline, nejbližší soused (pixel art). Vlastní oddělitelné převzorkování na všech jádrech (108 MPx na polovinu ~0,3 s). **Doostřit po převzorkování** (volitelné).
- Informace o nové velikosti, rozměru v cm a paměti; upozornění, že zvětšení nepřidá detaily.

## E. Klávesové zkratky (doplněné)

Výchozí zkratky jdou změnit v Soubor › Klávesové zkratky (Etapa 5): pro každou akci hlavní a alternativní klávesa, vyhledávání, kontrola kolizí (dialog nedovolí uložit dvě akce na stejnou klávesu), „Vše vrátit na výchozí“. Uloženo v nastavení pod stabilním identifikátorem akce (`file.open`, `tool.rect`…), platí pro všechna okna. Popisky nástrojů ukazují aktuální klávesy.

| Zkratka | Funkce |
|---|---|
| Ctrl+V / Ctrl+Shift+V | Vložit / Nový ze schránky |
| Ctrl+C / Ctrl+Shift+C | Kopírovat jen podklad / Kopírovat vše (B3) |
| Ctrl+X / Ctrl+Shift+X | Vyjmout jen podklad / Vyjmout vše (B3) |
| Ctrl+Alt+C | Kopírovat celý výsledný obrázek |
| Ctrl+O, Ctrl+S, Ctrl+Shift+S | Otevřít, Uložit, Uložit jako |
| Ctrl+Z, Ctrl+Shift+Z **a Ctrl+Y** | Zpět, Znovu |
| Ctrl+W | Zavřít okno |
| Ctrl+A | Vybrat vše (Ukazatel: všechny objekty; jinak výběr oblasti přes celé plátno) |
| Enter / Esc | Potvrdit / zrušit operaci |
| Delete | viz D1 |
| Mezerník + tažení, prostřední tlačítko + tažení | Posun pohledu |
| Ctrl+0, Ctrl+1, Ctrl+plus/minus | Zoom |
| R nebo > / L nebo < | Otočit doprava / doleva o 90° (jako IrfanView) |
| H / V | Zrcadlit vodorovně / svisle |
| Ctrl+Shift+K | Oříznout na výběr |
| Ctrl+Q | Ukončit (zavře všechna okna) |
| Ctrl+H | Skrýt / zobrazit anotace |
| Ctrl+Alt+N | Panel poznámek |
| Ctrl+B / Ctrl+I / Ctrl+U | Při editaci textu: tučné / kurzíva / podtržení (označené znaky, jinak celý text) |
| `[` / `]` | Velikost štětce |
| Ctrl+R (i Ctrl+Alt+I) / Ctrl+Alt+Shift+I | Změna rozlišení / Velikost plátna |
| Ctrl+Alt+S | Uložit jako projekt (.rolidan) |
| Ctrl+Alt+R | Otočit o úhel |
| Ctrl+Shift+E / B / U / L | Vylepšit barvy / Jas a kontrast / Odstín a sytost / Úrovně |
| PgDn / PgUp, Home / End | Další / předchozí, první / poslední obrázek ve složce |
| Z / Ctrl+T | Zvětšovat malé obrázky na celé obrazovce / Náhledy (D11) |
| Ctrl+P | Tisk (D12) |
| C | Zachytit obrazovku (D13) |
| F, Enter, F11 / F5 | Celá obrazovka / Prezentace (D11). Enter jen tehdy, když nástroj nemá co potvrdit (ořez, lomená čára) a není výběr oblasti. |
| Ctrl+I, Alt+Enter | Informace o obrázku |
| F2 / F7 / F8 / Ctrl+Delete | Přejmenovat / Přesunout do složky / Kopírovat do složky / Do koše |
| Shift+V Ukazatel · M Výběr oblasti · Y Výřez · T Text · A Šipka · Shift+L Čára · Shift+R Obdélník · O Elipsa · B Štětec · Shift+H Zvýrazňovač · S Razítko · E Guma · U Rozmazávací štětec · I Kapátko · K Zakrytí · N Číslovaná značka · P Lomená čára · G Mnohoúhelník · Q Bublina · J Pixelizační štětec · Shift+F Plechovka · W Narovnat | Nástroje (vypnuté při editaci textu). **Shift + písmeno vybere nástroj vždy**; samotné písmeno tam, kde ho nepoužívá prohlížeč (R, L, H, V, F). |

---

## F. Ukládání a export

### F1. Dokument ze schránky

Ctrl+S otevře dialog Uložit jako s navrženým jménem `image-2026-11-03-084512.png` (jazykově neutrální, viz L) v naposledy použité složce. Výchozí formát je PNG.

### F2. Dokument otevřený ze souboru

První Ctrl+S, které by přepsalo původní soubor, se zeptá: „Přepsat původní soubor? Anotace budou sloučeny do obrázku.“ U ztrátových formátů (JPG, ztrátový WebP) ještě „Soubor bude znovu ztrátově komprimován.“ Volba „Pro tento dokument se už neptat“. Další Ctrl+S ukládá bez dotazu.

### F3. Bezpečný zápis

Zápis přes `QSaveFile` (dočasný soubor + atomické přejmenování). Při chybě zůstane původní soubor nedotčený a dokument v editoru beze změny. Chybová hláška uvede důvod (práva, místo na disku, nepodporovaný formát).

### F4. Přípona a formát

- Uživatel napíše jméno bez přípony → doplní se podle zvoleného filtru.
- Napíše podporovanou příponu odlišnou od filtru → platí přípona.
- Napíše nepodporovanou příponu → doplní se přípona filtru (`foto.xyz.png`).
- Seznam formátů se sestaví za běhu z `QImageWriter::supportedImageFormats()`. Bez pluginu WebP (balík `qt6-image-formats-plugins`) se WebP nenabídne.

### F5. Formáty

| Formát | Volby | Poznámky k implementaci |
|---|---|---|
| PNG | komprese 0–9 (výchozí 6) | zachová alfu |
| JPG | kvalita (výchozí 90), podvzorkování barev 4:4:4 / 4:2:2 / 4:2:0 (výchozí 4:4:4 kvůli ostrému barevnému textu ve screenshotech), progresivní zápis, optimalizované Huffmanovy tabulky (výchozí zapnuto), vyhlazení 0–100, barva pozadí pro průhledná místa (výchozí bílá), náhled s odhadem velikosti souboru | alfa se sloučí na pozadí. Kóduje se přímo přes `libjpeg` (Qt plugin podvzorkování nenabízí); bez hlaviček libjpeg při sestavení zůstane jen kvalita, progresivní zápis a optimalizace. |
| WebP | ztrátový / bezeztrátový. Ztrátový: kvalita 0–100, typ obsahu (předvolby libwebp: obecný, obrázek, fotka, kresba, ikona, text), úsilí 0–6, kvalita průhlednosti, přesný převod barev (sharp YUV). Bezeztrátový: úroveň komprese 0–9, téměř bezeztrátový režim. Volitelně zachovat barvy zcela průhledných pixelů. | Kóduje se přímo přes `libwebp`. Odhad velikosti a náhled se počítají na pozadí, dialog nezamrzá. |
| ICO | velikosti 16, 24, 32, 48, 64, 128, 256 (multivýběr). Výchozí 16+32+48+256. | Maximum je 256×256. Neštvercový obrázek se vystředí na průhledný čtverec. `QImageWriter` zapisuje jen jeden obrázek, proto **vlastní zapisovač ICO** (ICONDIR + položky ve formátu PNG, jednoduché). |
| BMP | 24bitový | alfa se sloučí na pozadí jako u JPG |
| TIFF | komprese LZW (výchozí zapnuto) | zachová alfu; plugin Qt (qt6-image-formats-plugins) |
| AVIF, HEIC, JPEG XL | kvalita 1–100 (AVIF a JPEG XL: 100 = bezeztrátově) s odhadem velikosti a náhledem | zachová alfu; KImageFormats (kimageformat6-plugins), nabízí se jen když je plugin k dispozici. Balíčky přibalují i pluginy libheif (`LIBHEIF_PLUGIN_PATH`). |
| QOI | – | bezeztrátový, zachová alfu (KImageFormats) |

**Otevírání** (Etapa 7): vše, co umí pluginy Qt a KImageFormats – navíc GIF, TGA, SVG, ICNS, PSD, XCF, OpenEXR, HDR, JPEG 2000, PCX, DDS, RAW fotoaparátů (CR2, NEF, ARW, DNG…), KRA, ORA atd. Platí i pro listování složkou a náhledy.

**Metadata** (EXIF včetně GPS) se do exportu **nepřenášejí**. Je to vědomé rozhodnutí kvůli soukromí; uvést v dokumentaci.

### F6. Neuložené změny a editovatelnost

- Úspěšný export smaže příznak neuložených změn.
- Při zavření okna s neuloženými změnami se zobrazí dialog Uložit / Zahodit / Zrušit.
- Dokud neexistuje projektový formát: pokud dokument obsahuje objekty, stavový řádek trvale ukazuje „Anotace jsou editovatelné jen do zavření“. Dialog se kvůli tomu po úspěšném exportu znovu neukazuje.

### F7. Projektový formát (Etapa 4, model ho nesmí blokovat)

ZIP: `base.png` + `document.json` (objekty, verze formátu) + vložené obrázky jako PNG + `notes.md` (textové poznámky, jen pokud nejsou prázdné). Stejná serializace poslouží pro obnovu po pádu (periodický zápis do `~/.local/state/rolidan/`).

### F8. Textové poznámky k obrázku

Panel „Poznámky“ vpravo (Ctrl+Alt+N, menu Zobrazit, tlačítko v hlavní liště): volný text v Markdownu k obrázku (popis, úkoly, odkazy). Do obrázku se **nekreslí** a do exportovaných obrázků se nepřenáší (soukromí, viz F5 Metadata).

- Přepínač Úpravy / Náhled (naformátovaný Markdown, odkazy se otevřou v prohlížeči).
- Import .md / .markdown / .txt tlačítkem nebo přetažením souboru do okna. Pokud už poznámky existují, dotaz Přidat pod / Nahradit / Zrušit.
- Export do .md (UTF-8, výchozí název podle obrázku).
- Poznámky jsou součástí stavu dokumentu: psaní je jeden krok zpět (Ctrl+Z dokumentu), ukládají se do projektu (`notes.md`) a do obnovy po pádu; transformace obrázku je zachovají.
- Projekt s poznámkami otevře panel v režimu Náhled.

Terminologie v češtině: kreslené prvky (šipky, texty, tvary…) jsou **anotace**, **poznámky** jsou jen tento text.

---

## G. Historie, paměť, výkon

### G1. Ukládání kroků

| Typ kroku | Co se ukládá |
|---|---|
| Změna objektu | stará a nová hodnota vlastností (malé) |
| Tah razítka / gumy / štětcová úprava podkladu | výřez podkladu v ohraničujícím obdélníku před změnou |
| Rotace 90/180, zrcadlení | nic, operace je invertovatelná |
| Ořez, změna rozlišení, libovolná rotace, změna plátna | celý předchozí podklad + stav objektů |

Limit paměti historie je výchozí 1 GB (nastavitelný). Při překročení se zahazují nejstarší kroky. Příklad pro představu: jeden celý snímek 8K podkladu je cca 130 MB.

### G2. Měřitelné cíle (referenční stroj: 4jádrový notebook, integrovaná grafika)

| Ukazatel | Cíl |
|---|---|
| Spuštění → okno připravené na Ctrl+V | < 500 ms |
| Ctrl+V screenshotu 3840×2160 → zobrazeno | < 200 ms |
| Odezva štětce / razítka 100 px na 4K podkladu | zachovat 60 snímků/s |
| Největší podporovaný obrázek | 16384×16384 |
| Spotřeba paměti po vložení 4K screenshotu | < 200 MB |

Vykreslování plátna: podklad se rozdělí na dlaždice (např. 512×512) a překresluje se jen změněná oblast. Kompozit objektů se cachuje pro každý objekt zvlášť.

---

## H. Bezpečnost a soukromí

### H1. Zakrývání citlivých údajů

- **Rozmazání a mozaika nejsou spolehlivé** pro skrytí textu (hesla, čísla účtů); z nízkého rozlišení lze text někdy zrekonstruovat. U obou nástrojů se zobrazí nenápadná poznámka „Pro citlivé údaje použijte Zakrytí“.
- Rozmazání a mozaika jsou proto destruktivní úpravy podkladu. Neprůhledné zakrytí je objekt; při exportu i kopírování do schránky se vždy sloučí, takže zakrytá data v souboru ani ve schránce nezůstanou.
- Až vznikne projektový formát: při jeho ukládání upozornit, že pod zakrytím zůstávají původní data.

### H2. Ostatní

- Metadata se neexportují (F5).
- Aplikace sama nic nestahuje z internetu (B1/5).

---

## I. Etapy a akceptační kritéria

### Etapa 1 — základní workflow a model dokumentu

Rozsah: prázdné okno, Ctrl+V / Ctrl+Shift+V, otevření souboru, přetažení, parametry příkazové řádky, zoom a posun, nástroj Ukazatel + objekt „vložený obrázek“ (přesun, změna velikosti se zamčeným poměrem), Výřez, Výběr oblasti, rotace 90/180, zrcadlení, Zpět/Znovu, export PNG/JPG/WebP, kopírování a vyjímání podle B3, chování schránky po zavření.

Akceptace:
- [ ] Screenshot z GNOME / KDE (Wayland) i z X11 se vloží v plném rozlišení (ověřeno na displeji se 100% i 150% škálováním).
- [ ] „Kopírovat obrázek“ z Firefoxu a Chromia se vloží. Kopírování odkazu ukáže oznámení B1/5.
- [ ] Soubor zkopírovaný ve správci souborů (Nautilus, Dolphin) se vloží přes `text/uri-list`.
- [ ] PNG s průhledností z GIMPu projde vložením i zkopírováním zpět bez ztráty alfy.
- [ ] Výsledek zkopírovaný přes Ctrl+Alt+C (i přes Ctrl+C bez výběru) jde vložit do LibreOffice a do prohlížeče i **po zavření** Rolidanu, když je zapnutá volba J4. S vypnutou volbou se aplikace ukončí hned a zobrazí se oznámení.
- [ ] Ve výběru přes vložený objekt: Ctrl+C / Ctrl+X pracují jen s podkladem a objekt nechají; Ctrl+Shift+C / Ctrl+Shift+X pracují se vším podle B3.
- [ ] Ctrl+O nad rozpracovaným dokumentem otevře nové okno.
- [ ] Druhé Ctrl+V vloží objekt. Ořez, rotace a zrcadlení s ním zacházejí podle C4.
- [ ] Export PNG → znovu otevřít → pixelově totožné s kompozitem.
- [ ] Neúspěšný zápis (adresář bez práv) neztratí dokument ani nepoškodí cílový soubor.
- [ ] Fotka s EXIF orientací se zobrazí správně otočená.
- [ ] Obrázek 12000×9000 se načte.
- [ ] České znaky v názvech souborů a cestách fungují v otevírání, ukládání, přetažení i `text/uri-list`.
- [ ] Splněny cíle G2 pro spuštění a vložení.

### Etapa 2 — anotace

Rozsah: text, šipka, čára, obdélník, zaoblený obdélník, elipsa, štětec, zvýrazňovač (volný i obdélníkový), neprůhledné zakrytí, kapátko, mazání a duplikace objektů, pořadí, barvy, tloušťky, průhlednost, zapamatování nastavení nástrojů mezi spuštěními, sloučení objektu / všech objektů do podkladu.

Akceptace:
- [x] U každého typu objektu ověřeno chování podle C4 (ořez, rotace, zrcadlení, změna rozlišení).
- [x] Text: oprava překlepu dvojklikem po pěti dalších úpravách nevrací žádné z nich.
- [x] Text: psaní přes IME, mrtvé klávesy (č, ř, ů) a compose.
- [x] Zvýrazňovač: jeden pomalý tah přes sebe samý má všude stejný odstín (porovnání pixelů).
- [x] Tažení posuvníku barvy = jeden krok Zpět.
- [x] Export odpovídá zobrazení při 100 % (pixelové porovnání renderu pohledu a exportu).

### Etapa 3 — retuš a dokončení hlavního zadání

Rozsah: klonovací razítko (D5), pixelová guma, rozmazání oblasti, mozaika, rozmazávací štětec, změna rozlišení, změna plátna, export ICO a BMP.

Akceptace:
- [x] Razítko s krytím 50 % a mnoha překrývajícími se otisky v jednom tahu nikde nepřekročí 50 % (pixelový test).
- [x] Tvrdost 0 % dává plynulý přechod bez viditelného okraje; tvrdost 100 % ostrý okraj.
- [x] Zdroj zasahující mimo obrázek nevytváří artefakty.
- [x] Zarovnaný a nezarovnaný režim se chovají podle D5.
- [ ] Razítko o průměru 300 px na 4K podkladu drží 60 snímků/s. (Pixelový engine: nejhorší krok 0,8 ms v release sestavení, test `stamp300pxOn4kKeepsUp`. Plynulost vykreslení ověřit ručně na desktopu.)
- [ ] ICO se 4 velikostmi se správně zobrazí ve správci souborů a jako favicon v prohlížeči. (Struktura souboru ověřena testem `icoHasAllSizesAsPng`; zobrazení ověřit ručně.)

### Etapa 4 — rozšíření

Projektový formát a obnova po pádu (F7), libovolná rotace a narovnání podle čáry, formátování textu po znacích, mnohoúhelník, lomená čára a šipka, bublina, číslované značky, plechovka, jas/kontrast/sytost, doostření, procházení adresáře, dávkový export, vložení do vybraného obdélníku (přizpůsobit / roztáhnout).

Stav (2026-11-03), část A hotová: projektový formát `.rolidan` (ZIP bez komprese: `document.json`, `base.png`, `images/*.png`) a obnova po pádu (zápis do 5 s po změně, i u pouhého vloženého obrázku, do `~/.local/state/rolidan/recovery/`), číslované značky, lomená čára / šipka, mnohoúhelník, bublina, vložení obrázku do vybraného obdélníku (přizpůsobit, tlačítko „Roztáhnout“). Historie ukládá u úprav podkladu jen změněný obdélník (G1).
Doplněno 2026-11-03 po testu části A: obnova se zapisuje do 5 s po změně (dřív jednou za 30 s) a i pro nezměněný vložený obrázek; volby kodeků JPG/WebP (F5); vlastní ikony všech nástrojů a hlavní lišty (kreslené kódem, berou barvu z motivu; uložení = disketa), nástroje ve dvou sloupcích; textové poznámky k obrázku s importem/exportem .md (F8).
Část B (2026-11-03): rotace po desetinách stupně a narovnání podle čáry i automaticky podle textu, plechovka, pixelizační štětec, menu Barvy (vylepšit barvy, jas/kontrast, odstín/sytost, teplota, úrovně, šedá, sépie, inverze, práh, posterizace) a Filtry (rozmazat, doostřit, pixelizovat, odstranit šum, přidat šum / barevný šum, vyčistit naskenovaný dokument), procházení složky, dávkový převod (D7–D10).
Formátování textu po znacích (2026-11-03): Ctrl+B/I/U a panel nad označenou částí textu (D2). **Etapa 4 je hotová, vydáno jako verze 0.2.0 pod licencí GPL-3.0.**

### Etapa 5 — prohlížeč

Po testu uživatele (2026-11-03): šipky listují na celé obrazovce, přizpůsobení obrazovce s volbou zvětšit malé obrázky (Z), dvojklik a skrývání kurzoru na celé obrazovce, pás náhledů (Ctrl+T).
Klávesy podle IrfanView (2026-11-03, přání uživatele): F / Enter celá obrazovka, R / L a > / < otáčení, H / V zrcadlení, Ctrl+R změna rozlišení, Y výřez; nástroje R, L, H, V, F přes Shift. Vlastní klávesové zkratky (E).
Nedávné soubory, celá obrazovka, prezentace, kolečko myši pro listování, informace o obrázku s EXIF a GPS, přejmenování, kopírování a přesun do složky, koš (D11). Test hlídá jedinečné klávesové zkratky (podtržená písmena) v každém menu ve všech vestavěných jazycích.

Stav (2026-11-03): hotovo, otestováno automaticky.

- [ ] Prezentace na skutečném desktopu: celá obrazovka na monitoru, kde je okno.
- [ ] Koš: soubor se objeví v koši GNOME / KDE a jde obnovit.
- [ ] „Zobrazit na mapě“ otevře výchozí prohlížeč.

### Etapa 6 — tisk a otáčení objektů (verze 0.4.0)

Volné otáčení anotací myší (D1) a tisk s náhledem (D12).

Stav (2026-11-03): hotovo, otestováno automaticky (otáčení, změna velikosti otočeného objektu, Zpět, rozmístění na stránce, tisk do PDF).

- [ ] Tisk na skutečnou tiskárnu přes CUPS (GNOME / KDE).

### Etapa 7 — snímek obrazovky

Zachycení obrazovky, oblasti nebo okna tlačítkem v liště se zpožděním (D13); dialog Změnit velikost podle IrfanView (D14); gamma v dialogu Jas a kontrast; další formáty (F5); nápověda Ukazovátka zmiňuje otáčení.

Stav (2026-11-03): hotovo, automaticky otestováno zpracování odpovědi portálu a otevření snímku. Na balinu (bez grafického prostředí) nelze vyzkoušet samotné snímání.

- [ ] GNOME Wayland: celá obrazovka i výběr oblasti.
- [ ] KDE Plasma: totéž.
- [ ] X11 bez portálu (např. Xfce bez xdg-desktop-portal): celá obrazovka, oblast přes Oříznout.

---

## J. Rozhodnutí (odsouhlasena 2026-11-03)

| # | Otázka | Rozhodnutí |
|---|---|---|
| J1 | Ctrl+O / Ctrl+Shift+V nad neprázdným dokumentem | Otevře **nové okno**, stávající dokument zůstane. |
| J2 | Zrcadlení textu při zrcadlení dokumentu | Nezrcadlí se, mění se jen poloha. |
| J3 | „100 %“ na HiDPI | 1 pixel obrázku = 1 fyzický pixel. |
| J4 | Obsah schránky po zavření okna | **Volba v Nastavení**, výchozí zapnuto: proces běží na pozadí, dokud schránku nepřevezme jiná aplikace (B4). |
| J5 | Kopírování a vyjmutí výběru oblasti | **Ctrl+C / Ctrl+X** = jen podklad bez poznámek. **Ctrl+Shift+C / Ctrl+Shift+X** = vše (Shift+X smaže objekty celé uvnitř). Bez výběru Ctrl+C kopíruje celý výsledek. Ctrl+Alt+C = vždy celý výsledek. Viz B3. |
| J6 | Jazyk UI | Výchozí **angličtina**. Vestavěné v binárce: čeština, němčina, španělština, japonština. Další jazyky jako externí jazykové soubory. Viz L. |
| J7 | Distribuce | Nativní build pro Debian 13 (Qt 6.8) + AppImage. Flatpak později. |
| J8 | Název aplikace | Rolidan. |

## K. Poznámky k technologii (C++ / Qt 6)

| Oblast | Qt prostředky | Pozor |
|---|---|---|
| Schránka | `QClipboard`, `QMimeData::formats()` pro vlastní prioritu MIME (B1) | `QClipboard::image()` samo nerozliší URL a soubory, proto číst formáty ručně |
| Načtení | `QImageReader` (`setAutoTransform`, `setAllocationLimit`) | B7 |
| Zápis | `QImageWriter`, `QSaveFile` | ICO vlastní zapisovač, ověřit WebP lossless |
| Historie | `QUndoStack`, `QUndoCommand::mergeWith` | |
| Kreslení | `QPainter`, `CompositionMode_Multiply` pro zvýrazňovač | razítko a gumu implementovat nad vlastním bufferem masky, ne přes `QPainter` po otiscích (D5) |
| Text | `QTextLayout` / `QTextDocument` pro vykreslení, vlastní editace na plátně | IME přes `inputMethodEvent` / `inputMethodQuery` |
| Plátno | vlastní `QWidget` s dlaždicovým vykreslováním | Navrženo **nepoužít `QGraphicsScene` jako model**. Model (C1) musí být nezávislý na zobrazení kvůli historii, serializaci a rastrovým nástrojům. Pohled ho jen vykresluje. |
| Závislosti za běhu | `qt6-base`, `qt6-image-formats-plugins` (WebP), `qt6-wayland` | |

Sestavení: CMake, C++20, testy přes Qt Test. Pixelové testy renderu, razítka a zvýrazňovače běží headless s `QT_QPA_PLATFORM=offscreen`. Testy schránky na Waylandu a X11 jsou ruční podle checklistu v oddílu I.

---

## L. Lokalizace

- **Zdrojové texty v angličtině** (`tr()` / `QT_TR_NOOP`). Angličtina je výchozí jazyk UI bez ohledu na locale systému. Jazyk se mění v Nastavení.
- **Vestavěné jazyky:** `cs`, `de`, `es`, `ja`. Soubory `.qm` jsou zkompilované do binárky jako Qt resource (`qt_add_translations` v CMake), takže fungují i bez instalovaných datových souborů (AppImage). Spolu s nimi se vkládají i překlady standardních dialogů Qt (`qtbase_cs/de/es/ja.qm`).
- **Externí jazykové soubory:** `rolidan_<kód>.qm` (volitelně `qtbase_<kód>.qm`) hledané v pořadí:
  1. `~/.local/share/rolidan/translations/`
  2. `/usr/share/rolidan/translations/`
  3. adresář `translations/` vedle binárky (AppImage, přenosná verze)

  Seznam jazyků v Nastavení = vestavěné + nalezené externí. Externí soubor se stejným kódem jako vestavěný jazyk má přednost; tak lze opravit překlad bez nového buildu.
- Překladatelé dostanou šablonu `.ts` (z `lupdate`); dokumentace popíše, jak z ní udělat `.qm` (`lrelease`) a kam ho dát.
- Změna jazyka se projeví po restartu aplikace (v1). Přepínání za běhu není požadavek.
- Formáty čísel a data v UI (velikost souboru, rozměry) podle zvoleného jazyka přes `QLocale`. Jméno souboru navržené v F1 je jazykově neutrální: `image-2026-11-03-084512.png`.
- UI nesmí mít texty pevné šířky; němčina bývá o 30–40 % delší než angličtina. Kontrolovat rozvržení ve všech vestavěných jazycích.
- **Japonština** klade další požadavky:
  - textový nástroj musí fungovat s IME (fcitx5, IBus) včetně zobrazení rozpracovaného textu (preedit) přímo na plátně;
  - výchozí font textového nástroje musí mít záložní CJK font (`QFont` fallback, např. Noto Sans CJK), jinak se zobrazí čtverečky;
  - svislý text není v rozsahu v1.

Akceptace (Etapa 1, rozšíření):
- [ ] Přepnutí na každý vestavěný jazyk funguje bez externích souborů, včetně standardních dialogů Qt.
- [ ] Externí `rolidan_pl.qm` v `~/.local/share/rolidan/translations/` se objeví v nabídce a použije.
- [ ] V němčině se žádný popisek v panelu nástrojů ani v dialozích neořízne.

Akceptace (Etapa 2, text):
- [ ] Japonský text zadaný přes fcitx5 i IBus, s viditelným preeditem, se správně vykreslí na plátně i v exportu.
