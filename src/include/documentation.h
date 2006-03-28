#ifndef DOCUMENTATION_H
#define DOCUMENTATION_H

/**
 * \mainpage NuFW documentation (SVN version)
 *
 * \section intro_sec Introduction
 *
 * NuFW is a firewall based on NetFilter (http://www.netfilter.org)
 * which authentificate users. It's composed of three parts:
 *   - NuFW: gateway that works directly with NetFilter, just sends new
 *     connection packets to NuAuth, and applies decisions (accept or drop) ;
 *   - NuAuth: Kernel of the firewall, manages client connections, and takes
 *     decisions on new connection packets sent by NuFW ;
 *   - Client (nutcpc or Windows client): Authentificates users to NuAuth and
 *     answers NuAuth requests (sends its new connection list).
 *
 * This documentation only describes NuFW and NuAuth.
 * 
 * \section nufw_sec NuFW
 *
 * NuFW is a very simple gateway. It runs in user-space but needs root
 * privileges because it's connected directly to NetFilter using IPQ or Netfilter
 * queue link.
 *
 * NuFW is composed of three main parts:
 *    - Main process which just displays audit informations (number of received
 *      packets and number of accepted packets) every five seconds ; 
 *    - Packet server thread: packetsrv() ;
 *    - Auth server thread (connection to NuAuth): authsrv().
 *
 * \section nuauth_sec NuAuth
 *
 * NuAuth is the biggest and most important part of NuFW firewall:
 *    - Main loop: nuauth_main_loop()
 */

#endif  /* of ifndef DOCUMENTATION_H */

