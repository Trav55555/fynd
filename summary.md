| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 10.80 | 25.33 | 19.98 | 2.35× faster | 10.52 | 16.47 | 20.55 | 1.56× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 10.34 | 21.33 | 19.26 | 2.06× faster | 10.48 | 15.60 | 18.80 | 1.49× faster |
| deep: 768 files, 97 dirs | all files | 4.13 | 6.41 | 4.26 | 1.55× faster | 5.20 | 7.74 | 4.26 | 1.49× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 3.82 | 4.67 | 3.99 | 1.22× faster | 4.93 | 6.07 | 4.03 | 1.23× faster |
| mixed: 1,452 files, 364 dirs | all files | 4.84 | 6.94 | 7.13 | 1.43× faster | 3.41 | 6.12 | 7.12 | 1.79× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 4.70 | 6.61 | 6.88 | 1.41× faster | 3.34 | 6.15 | 6.97 | 1.84× faster |

Median wall time of 21 warm runs, process startup included. fynd at 5d906b1; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
