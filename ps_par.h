#ifndef __PS_PAR_H__
#define __PS_PAR_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @fn void ps_par (int fd, char * str, bool is_data)
 * 
 * @param [in] fd - ch341device file descriptor
 * @param [in] str - String containing instruction/data
 * @param [in] is_data - Boolean to tell if str contains data (1 for data, 0 for instruction)
 * 
 * @return None
 * 
 * @brief This function is used to write in CH34x SPP mode.
 */
void ps_par (int fd, char * str, bool is_data);

/**
 * @fn void ps_close (int fd)
 * 
 * @param [in] fd - ch341device file descriptor
 *
 * @return None
 *
 * @brief This function is used to close CH34x SPP mode.
 */
void ps_close (int fd);

#ifdef __cplusplus
}
#endif

#endif