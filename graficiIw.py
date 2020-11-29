import matplotlib.pyplot as plt

if __name__ == '__main__':

   y_p0_td=[11.39,7.45,13.25,28.6,244.73]
   x_p0_td=[1,5,20,100,1000]
   plt.xlabel("N")
   plt.ylabel("ms")
   plt.title("Get con P=0")
   plt.plot(x_p0_td,y_p0_td,'b', label="T dinamico")
   y_p0_t10 = [7.65,2.17,2.13,2.45,3.31]
   x_p0_t10 = [1, 5, 20, 100, 1000]
   plt.plot(x_p0_t10, y_p0_t10, 'r', label="T 10ms")
   y_p0_100 = [8.59,2.45,3.55,2.17,2.65]
   x_p0_100 = [1, 5, 20, 100, 1000]
   plt.plot(x_p0_100, y_p0_100, 'g', label="T 100ms")
   y_p0_1000 = [11.39,2.03,2.53,2.83,2.34]
   x_p0_1000 = [1, 5, 20, 100, 1000]
   plt.plot(x_p0_1000, y_p0_1000, 'y', label="T 1000ms")
   plt.legend(["T dinamico", "T 10ms", "T 100ms", "T 1000ms"])
   plt.show()