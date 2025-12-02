console.log("Javascript loaded successfully!");

function honk() {
    const nose = document.querySelector('.clown-nose');
    const title = document.querySelector('h1');
    
    // Change nose color randomly
    const colors = ['#ff4757', '#eccc68', '#7bed9f', '#70a1ff'];
    const randomColor = colors[Math.floor(Math.random() * colors.length)];
    
    nose.style.background = randomColor;
    
    // Shake the title
    title.style.transform = "rotate(5deg)";
    setTimeout(() => {
        title.style.transform = "rotate(0deg)";
    }, 100);

    alert("Honk! Honk! 🤡\nO JavaScript foi carregado corretamente!");
}