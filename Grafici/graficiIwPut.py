import matplotlib.pyplot as plt

if __name__ == '__main__':

   y_td=[8.74, 6.14, 13.24, 23.4, 185.37]
   x_td=[1, 5, 20, 100, 1000]
   plt.xlabel("N")
   plt.ylabel("ms")
   plt.title("Put con P=0")
   plt.plot(x_td, y_td, 'b', label="T dinamico")
   y_t10 = [7.12, 1.81, 1.85, 1.9, 1.82]
   x_t10 = [1, 5, 20, 100, 1000]
   plt.plot(x_t10, y_t10, 'r', label="T 10ms")
   y_t100 = [7.34, 1.82, 1.78, 1.74, 1.79]
   x_t100 = [1, 5, 20, 100, 1000]
   plt.plot(x_t100, y_t100, 'g', label="T 100ms")
   y_t1000 = [8.27, 1.7, 1.83, 1.75, 1.96]
   x_t1000 = [1, 5, 20, 100, 1000]
   plt.plot(x_t1000, y_t1000, 'y', label="T 1000ms")
   plt.legend(["T dinamico", "T 10ms", "T 100ms", "T 1000ms"])
   plt.show()