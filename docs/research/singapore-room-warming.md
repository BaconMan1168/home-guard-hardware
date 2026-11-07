# How fast do Singapore rooms warm after the AC goes off?

Research date: 2026-11-07. Question: can published data give a trustworthy **starting warming time constant (τ)** for HDB flats, condos and landed homes, so power-toggle remotes could get Turn it off before a room has learned its own τ?

**Short answer: not yet.** No published study measures how fast a Singapore bedroom warms after the AC is switched off. The general building-physics literature gives a range so wide that any cautious starting value blocks toggle sends anyway. "Wait until learned" stays the rule, and the data supports it.

## What we found

### No Singapore switch-off measurements

Searches for HDB, condo and landed-home bedroom warming after AC switch-off found nothing direct. The closest Singapore studies measure related things:

- **AC bedrooms run at 23–28 °C, naturally ventilated ones at 29–30 °C** (paired-bedroom IAQ study in Singapore homes). This sets the typical room/outdoor gap. [CIB / Indoor Air 2002](https://www.irbnet.de/daten/iconda/CIB7451.pdf)
- **Average night-time bedroom temperature is about 28.4 °C, with AC used only a little over half the time at night** (150-resident study, 2026). [Yahoo News SG](https://sg.news.yahoo.com/temperature-bedroom-pore-muggy-28-075000681.html)
- **Night minimums rarely fall below 23–25 °C.** Outdoor air at night is often only a few degrees above an AC'd room, which is exactly the weak spot described in the design.
- **Nearly half of 416 HDB flats were warmer indoors than outdoors** (SUTD, 2024–25, living rooms, heat index; mainly poor cross-ventilation). The weather-station outdoor reading the app uses can therefore be **lower** than the temperature an AC-off room actually drifts toward. [SUTD](https://www.sutd.edu.sg/news-listing/no-escape-from-the-heat-even-at-home-experts-urge-people-to-adjust-their-daily-routines), [Her World](https://www.herworld.com/life/your-home-hotter-outside-what-singapores-latest-housing-study-reveals)
- An NUS/SMU bedroom field study (18 participants, 30+ nights each) logged bedroom air temperature under dynamic AC control, but reports control fidelity, not warming rates. [SMU InK](https://ink.library.smu.edu.sg/cis_research/655/)
- Simulation work shows AC waste heat can raise night-time air temperature in neighbouring naturally ventilated HDB flats by up to about 4 °C. [Energy and Buildings 2022](https://www.sciencedirect.com/science/article/abs/pii/S0378778822003425)

### General building physics

- **Rooms have two time constants:** a fast one (indoor air settling toward the wall temperature, minutes) and a slow one (walls and structure, hours). A single-τ model only approximates whichever phase it is fitted on. [OSTI second-order home model](https://www.osti.gov/pages/servlets/purl/1842207), [Kircher, thermal modelling notes](https://kevinjkircher.com/wp-content/uploads/2025/02/der-buildings-2.pdf), [Energy and Buildings 2019](https://www.sciencedirect.com/science/article/abs/pii/S0378778818338350)
- **Measured single-τ values for whole homes range from under 1 hour to about 18 hours** in summer (US statistical study). [IBPC / Syracuse](https://surface.syr.edu/cgi/viewcontent.cgi?article=1283&context=ibpc)
- **Heavy thermal mass gives time constants around 6 hours.** Concrete flats like HDB sit at the heavy end. [Thermal inertia review, U. Antwerp](https://repository.uantwerpen.be/docman/irua/72fb5d/147988_2019_09_02.pdf)
- A clean fit needs free-floating periods of at least 1.5–3 hours with fairly steady outdoor temperature. [Thermal time constant of school buildings](https://www.sciencedirect.com/science/article/abs/pii/S2352710225005480)

## What this means for HomeGuard

1. **A cautious literature prior blocks toggle sends anyway.** The toggle rule needs `(outdoor − room) × (1 − e^(−15/τ)) ≥ 0.5 °C`. Here is the outdoor/room gap each τ would require:

   | τ | Required gap | Possible in Singapore? |
   |---|---|---|
   | 1 h | 2.3 °C | Yes |
   | 2 h | 4.3 °C | Sometimes, mainly daytime |
   | 6 h (heavy concrete) | ≈ 12 °C | No |
   | 18 h (literature max) | ≈ 36 °C | No |

   A safe starting value has to come from the slow end of the range, and at the slow end the rule can never pass. So "wait until learned" isn't just a placeholder: it's what the evidence supports.

2. **The weather-station bias errs on the safe side.** If the room actually drifts toward a temperature *above* the outdoor reading (common in HDB flats), an AC-off room warms faster than the app predicts. It then fails the "stable" test sooner, so a false `likely-on` gets less likely, not more.

3. **The learned τ matches the phase the check runs in.** Calibration fits τ from about 20 to 120 minutes after shut-off. The toggle check runs at least 20 minutes after departure, on a 15-minute window. That's the same slow phase, so using the learned τ there is consistent.

4. **The best prior will be HomeGuard's own data.** Once hubs are in homes, every automatic shut-off calibration is a real Singapore measurement. Grouped by home type (HDB, condo, landed) and facing, these could give a defensible starting τ for new users. That's future work: it needs an opt-in to share anonymized τ values, which is out of scope now.

## Possible next step (not scheduled)

When the first hub exists, log a few switch-off warming curves in an HDB bedroom and a condo bedroom (AC off, door closed, 2–3 hours, daytime and night) to see the real τ range before relying on any prior.
