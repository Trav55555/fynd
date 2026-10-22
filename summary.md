| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 10.98 | 25.77 | 20.34 | 2.35× faster | 10.77 | 16.71 | 20.92 | 1.55× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 10.47 | 21.68 | 18.86 | 2.07× faster | 10.64 | 15.90 | 20.04 | 1.49× faster |
| deep: 768 files, 97 dirs | all files | 4.20 | 6.61 | 4.34 | 1.58× faster | 5.49 | 8.03 | 4.42 | 1.46× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 3.88 | 4.72 | 4.07 | 1.22× faster | 5.18 | 6.27 | 4.19 | 1.21× faster |
| mixed: 1,452 files, 364 dirs | all files | 5.01 | 7.05 | 7.19 | 1.41× faster | 3.54 | 5.54 | 7.37 | 1.56× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 4.80 | 6.77 | 7.02 | 1.41× faster | 3.40 | 5.84 | 7.21 | 1.72× faster |

Median wall time of 21 warm runs, process startup included. fynd at 4462734; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
