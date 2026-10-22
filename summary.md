| Tree | Query | fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 10.57 | 25.11 | 18.53 | 2.38× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 10.19 | 21.05 | 17.96 | 2.06× faster |
| deep: 768 files, 97 dirs | all files | 4.01 | 6.25 | 4.19 | 1.56× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 3.78 | 4.60 | 3.97 | 1.22× faster |
| mixed: 1,452 files, 364 dirs | all files | 4.76 | 6.83 | 7.01 | 1.44× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 4.67 | 6.58 | 6.85 | 1.41× faster |

Median wall time of 21 warm runs, process startup included. fynd -j1 at 012f2e1; fdfind 9.0.0 with one thread; find (GNU findutils) 4.9.0. GitHub-hosted ubuntu-24.04 runner, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
