// Balanced-only Nero against the 15 verified ground target families.
static int NeroGroundDestination(bool balanced,int source,int loaded)
{
    if(!balanced) return -1;
    switch(source) {
    case 70:return loaded==70 ? 7300 : -1;
    case 86:return loaded==86 ? 7301 : -1;
    case 118:return loaded==118 ? 7302 : -1;
    case 134:return loaded==134 ? 7303 : -1;
    case 150:return loaded==150 ? 7304 : -1;
    case 166:return loaded==166 ? 7305 : -1;
    case 182:return loaded==182 ? 7306 : -1;
    case 198:return loaded==1198 ? 7307 : -1;
    case 202:return loaded==202 ? 7308 : -1;
    case 204:return loaded==204 ? 7309 : -1;
    case 206:return loaded==206 ? 7310 : -1;
    case 208:return loaded==208 ? 7311 : -1;
    case 209:return loaded==209 ? 7312 : -1;
    case 210:return loaded==210 ? 7313 : -1;
    case 211:return loaded==211 ? 7314 : -1;
    case 212:return loaded==212 ? 7315 : -1;
    case 213:return loaded==1213 ? 7316 : -1;
    case 215:return loaded==215 ? 7317 : -1;
    case 216:return loaded==1216 ? 7318 : -1;
    case 217:return loaded==1217 ? 7319 : -1;
    case 262:return loaded==262 ? 7320 : -1;
    case 278:return loaded==278 ? 7321 : -1;
    case 294:return loaded==294 ? 7322 : -1;
    case 326:return loaded==326 ? 7323 : -1;
    case 358:return loaded==1358 ? 7324 : -1;
    case 423:return loaded==423 ? 7325 : -1;
    case 425:return loaded==425 ? 7326 : -1;
    case 427:return loaded==1427 ? 7327 : -1;
    case 490:return loaded==490 ? 7328 : -1;
    case 506:return loaded==506 ? 7329 : -1;
    case 538:return loaded==538 ? 7330 : -1;
    case 554:return loaded==554 ? 7331 : -1;
    case 570:return loaded==570 ? 7332 : -1;
    case 586:return loaded==586 ? 7333 : -1;
    case 602:return loaded==602 ? 7334 : -1;
    case 618:return loaded==1618 ? 7335 : -1;
    case 622:return loaded==622 ? 7336 : -1;
    case 624:return loaded==624 ? 7337 : -1;
    case 626:return loaded==626 ? 7338 : -1;
    case 628:return loaded==628 ? 7339 : -1;
    case 629:return loaded==629 ? 7340 : -1;
    case 630:return loaded==630 ? 7341 : -1;
    case 631:return loaded==631 ? 7342 : -1;
    case 632:return loaded==632 ? 7343 : -1;
    case 633:return loaded==1633 ? 7344 : -1;
    case 635:return loaded==635 ? 7345 : -1;
    case 636:return loaded==1636 ? 7346 : -1;
    case 637:return loaded==1637 ? 7347 : -1;
    case 682:return loaded==682 ? 7348 : -1;
    case 698:return loaded==698 ? 7349 : -1;
    case 714:return loaded==714 ? 7350 : -1;
    case 746:return loaded==746 ? 7351 : -1;
    case 778:return loaded==1778 ? 7352 : -1;
    case 843:return loaded==843 ? 7353 : -1;
    case 845:return loaded==845 ? 7354 : -1;
    case 847:return loaded==1847 ? 7355 : -1;
    default:return -1;
    }
}
